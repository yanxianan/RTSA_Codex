`timescale 1ns / 1ps

// 频谱处理模块
//
// 本模块只负责数据通路连接和统一复位控制：
// 1. dec_module完成DDS/RFADC选择以及CIC、HB抽取；
// 2. overlap_fft_module完成重叠组帧、加窗、FFT和最大保持；
// 3. trigger_module根据原始FFT结果产生频率触发，并保存触发前后时域IQ；
// 4. 模式切换时短暂复位整条处理链，避免一帧中混入两个数据源。
//
// 实时频谱和触发捕获数据分别使用独立AXI4-Stream端口输出，
// 后续可在DMA_TOP中根据工作模式选择其中一路送往DMA。
module spectrum_engine #(
    parameter integer MODE_RESET_R_CYCLES = 32
)(
    // w_clk/w_rst暂时作为兼容端口保留。
    // 当前重叠组帧的读写均已统一使用r_clk。
    input  wire         w_rst,
    input  wire         w_clk,
    input  wire         r_rst,
    input  wire         r_clk,

    // 数据源及DDS配置。
    input  wire [3:0]   dma_mode,
    input  wire         dds_resync,
    input  wire [15:0]  dds_phase_step_h,
    input  wire [15:0]  dds_phase_step_l,

    // CIC、HB和最大保持配置。
    input  wire [15:0]  cic_rate_cfg,
    input  wire [7:0]   cic_trunc_cfg,
    input  wire [2:0]   hb_level,
    input  wire [15:0]  max_hold_frames_cfg,
    input  wire         cic_cfg_valid,
    output wire         cic_cfg_busy,
    input  wire         config_datapath_reset,

    // RFADC Tile 0：m00为I，m01为Q。
    input  wire [63:0]  m00_axis_tdata,
    input  wire         m00_axis_tvalid,
    output wire         m00_axis_tready,
    input  wire [63:0]  m01_axis_tdata,
    input  wire         m01_axis_tvalid,
    output wire         m01_axis_tready,

    // RFADC Tile 1：m10为I，m11为Q。
    input  wire [63:0]  m10_axis_tdata,
    input  wire         m10_axis_tvalid,
    output wire         m10_axis_tready,
    input  wire [63:0]  m11_axis_tdata,
    input  wire         m11_axis_tvalid,
    output wire         m11_axis_tready,

    // 频率触发配置。
    input  wire         trigger_enable,
    input  wire         trigger_clear,
    input  wire [9:0]   trigger_start_bin,
    input  wire [9:0]   trigger_end_bin,
    input  wire [63:0]  trigger_power_threshold,

    // 触发前后时域IQ捕获配置。
    input  wire         capture_arm,
    input  wire         capture_clear,
    input  wire [31:0]  pre_sample_num,
    input  wire [31:0]  post_sample_num,

    // 频率触发结果和捕获状态。
    output wire         trigger_pulse,
    output wire         trigger_latched,
    output wire [9:0]   trigger_bin,
    output wire [63:0]  trigger_power,
    output wire [63:0]  trigger_threshold,
    output wire         capture_busy,
    output wire         capture_ready,
    output wire         capture_triggered,
    output wire         capture_done,
    output wire [31:0]  captured_sample_num,

    // 触发后从捕获RAM读出的时域IQ数据。
    input  wire         trigger_axis_tready,
    output wire [63:0]  trigger_axis_tdata,
    output wire [7:0]   trigger_axis_tkeep,
    output wire         trigger_axis_tvalid,
    output wire         trigger_axis_tlast,

    // 最大保持后的频谱数据，送往AXI4-Stream FIFO和DMA。
    input  wire         m_axis_data_tready,
    output wire [63:0]  m_axis_data_tdata,
    output wire         m_axis_data_tvalid,
    output wire         m_axis_data_tlast
);

localparam [3:0] MODE_IDLE = 4'd0;
localparam [3:0] MODE_DDS  = 4'd1;
localparam [3:0] MODE_ADC0 = 4'd2;
localparam [3:0] MODE_ADC1 = 4'd3;

// -------------------------------------------------------------------------
// 数据源切换复位
// -------------------------------------------------------------------------
// dma_mode变化后，r_clk域处理链保持复位MODE_RESET_R_CYCLES个周期。
// 这样可以清除旧数据源残留在CIC、HB、组帧缓存和FFT中的数据。
reg [3:0] dma_mode_r_d;
reg [7:0] mode_reset_r_count;

always @(posedge r_clk or negedge r_rst) begin
    if (!r_rst) begin
        dma_mode_r_d       <= MODE_IDLE;
        mode_reset_r_count <= 8'd0;
    end
    else begin
        dma_mode_r_d <= dma_mode;

        if (dma_mode != dma_mode_r_d)
            mode_reset_r_count <= MODE_RESET_R_CYCLES[7:0];
        else if (mode_reset_r_count != 8'd0)
            mode_reset_r_count <= mode_reset_r_count - 1'b1;
    end
end

wire mode_reset_r = (mode_reset_r_count != 8'd0);
wire datapath_r_rst_n = r_rst && !mode_reset_r && !config_datapath_reset;

// 当前模式确实选择了某个数据源时，才允许重叠组帧模块接收数据。
wire source_enabled = (dma_mode == MODE_DDS)  ||
                      (dma_mode == MODE_ADC0) ||
                      (dma_mode == MODE_ADC1);

// -------------------------------------------------------------------------
// DDS/RFADC选择与抽取滤波
// -------------------------------------------------------------------------
wire [63:0] dec_data;
wire        dec_valid;

(* mark_debug = "true" *) wire rfdc_valid_mismatch;

// CIC=1 is the wideband bypass mode. At 312.5/156.25 MSPS the frame
// controller cannot read a full 1024-point frame and also start a new
// 75%-overlap frame every 256 samples, so use 0% overlap in bypass mode.
// Narrowband keeps the original 768-sample overlap.
wire [15:0] overlap_ratio_cfg =
    (cic_rate_cfg == 16'd1) ? 16'd0 : 16'd768;

dec_module u_dec_module (
    .clk                 (r_clk),
    .rst_n               (r_rst),
    .datapath_rst_n      (datapath_r_rst_n),

    .dma_mode            (dma_mode),
    .dds_resync          (dds_resync),
    .dds_phase_step_h    (dds_phase_step_h),
    .dds_phase_step_l    (dds_phase_step_l),

    .cic_rate_cfg        (cic_rate_cfg),
    .cic_trunc_cfg       (cic_trunc_cfg),
    .hb_level            (hb_level),
    .cic_cfg_valid       (cic_cfg_valid),
    .cic_cfg_busy        (cic_cfg_busy),

    .m00_axis_tdata      (m00_axis_tdata),
    .m00_axis_tvalid     (m00_axis_tvalid),
    .m00_axis_tready     (m00_axis_tready),
    .m01_axis_tdata      (m01_axis_tdata),
    .m01_axis_tvalid     (m01_axis_tvalid),
    .m01_axis_tready     (m01_axis_tready),

    .m10_axis_tdata      (m10_axis_tdata),
    .m10_axis_tvalid     (m10_axis_tvalid),
    .m10_axis_tready     (m10_axis_tready),
    .m11_axis_tdata      (m11_axis_tdata),
    .m11_axis_tvalid     (m11_axis_tvalid),
    .m11_axis_tready     (m11_axis_tready),

    .data_out            (dec_data),
    .data_out_valid      (dec_valid),
    .rfdc_valid_mismatch (rfdc_valid_mismatch)
);

// -------------------------------------------------------------------------
// 重叠组帧、加窗、FFT及最大保持
// -------------------------------------------------------------------------
// 原始FFT流一方面进入最大保持模块，另一方面送给频率触发模块检测。
(* mark_debug = "true" *) wire [63:0] fft_raw_tdata;
(* mark_debug = "true" *) wire        fft_raw_tvalid;
(* mark_debug = "true" *) wire        fft_raw_tready;
(* mark_debug = "true" *) wire        fft_raw_tlast;

(* mark_debug = "true" *) wire        frame_drop_pulse;
(* mark_debug = "true" *) wire [31:0] frame_drop_count;
(* mark_debug = "true" *) wire        fft_fifo_overflow_sticky;

overlap_fft_module u_overlap_fft_module (
    .clk                      (r_clk),
    .rst_n                    (datapath_r_rst_n),
    .source_enabled           (source_enabled),

    .data_in                  (dec_data),
    .data_in_valid            (dec_valid),
    .max_hold_frames_cfg      (max_hold_frames_cfg),
    .overlap_ratio_cfg        (overlap_ratio_cfg),

    .m_axis_data_tready       (m_axis_data_tready),
    .m_axis_data_tdata        (m_axis_data_tdata),
    .m_axis_data_tvalid       (m_axis_data_tvalid),
    .m_axis_data_tlast        (m_axis_data_tlast),

    .fft_raw_tdata            (fft_raw_tdata),
    .fft_raw_tvalid           (fft_raw_tvalid),
    .fft_raw_tready           (fft_raw_tready),
    .fft_raw_tlast            (fft_raw_tlast),

    .frame_drop_pulse         (frame_drop_pulse),
    .frame_drop_count         (frame_drop_count),
    .fft_fifo_overflow_sticky (fft_fifo_overflow_sticky)
);

// -------------------------------------------------------------------------
// 频率触发与触发前后时域IQ捕获
// -------------------------------------------------------------------------
// trigger_module使用高有效复位；频谱处理链复位、模式切换复位期间，
// 同时清空频率检测状态和触发捕获状态。
trigger_module u_trigger_module (
    .clk                 (r_clk),
    .rst                 (!datapath_r_rst_n),

    // 在最大保持之前检测每一帧原始FFT数据。
    .fft_tdata           (fft_raw_tdata),
    .fft_tvalid          (fft_raw_tvalid),
    .fft_tready          (fft_raw_tready),
    .fft_tlast           (fft_raw_tlast),

    // RAM中保存抽取滤波后的时域IQ数据。
    .time_data           (dec_data),
    .time_data_valid     (dec_valid),

    .trigger_enable      (trigger_enable),
    .trigger_clear       (trigger_clear),
    .start_bin           (trigger_start_bin),
    .end_bin             (trigger_end_bin),
    .power_threshold     (trigger_power_threshold),

    .trigger_pulse       (trigger_pulse),
    .trigger_latched     (trigger_latched),
    .trigger_bin         (trigger_bin),
    .trigger_power       (trigger_power),
    .trigger_threshold   (trigger_threshold),

    .capture_arm         (capture_arm),
    .capture_clear       (capture_clear),
    .pre_sample_num      (pre_sample_num),
    .post_sample_num     (post_sample_num),

    .m_axis_tdata        (trigger_axis_tdata),
    .m_axis_tkeep        (trigger_axis_tkeep),
    .m_axis_tvalid       (trigger_axis_tvalid),
    .m_axis_tready       (trigger_axis_tready),
    .m_axis_tlast        (trigger_axis_tlast),

    .capture_busy        (capture_busy),
    .capture_ready       (capture_ready),
    .capture_triggered   (capture_triggered),
    .capture_done        (capture_done),
    .captured_sample_num (captured_sample_num)
);

endmodule
