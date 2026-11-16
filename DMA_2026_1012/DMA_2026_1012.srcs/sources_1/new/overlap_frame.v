`timescale 1ns / 1ps

// DDS/RFADC selectable overlap-frame FFT pipeline.
//
// dma_mode:
//   4'd0 : idle
//   4'd1 : internal DDS
//   4'd2 : RFADC tile 0 (m00 = I, m01 = Q)
//   4'd3 : RFADC tile 1 (m10 = I, m11 = Q)
//
// Each RFADC stream carries four signed 16-bit samples. The RFADC samples are
// formed from signed 14-bit data with two zero LSBs, so [15:2] recovers the
// original signed 14-bit sample without changing its scale.
module overlap_frame #(
    parameter integer MODE_RESET_R_CYCLES = 32,
    parameter integer MODE_RESET_W_CYCLES = 4
)(
    input  wire         w_rst,               // Active-low, synchronous to w_clk after release
    input  wire         w_clk,               // 312.5 MHz / 32 = 9.765625 MHz
    input  wire         r_rst,               // Active-low, synchronous to r_clk after release
    input  wire         r_clk,               // 312.5 MHz

    input  wire [3:0]   dma_mode,
    input  wire         dds_resync,
    input  wire [15:0]  dds_phase_step_h,
    input  wire [15:0]  dds_phase_step_l,

    // PS配置的抽取滤波参数。I/Q两路必须同时使用同一组配置。
    input  wire [15:0]  cic_rate_cfg,
    input  wire [7:0]   cic_trunc_cfg,
    input  wire [2:0]   hb_level,
    input  wire [15:0]  max_hold_frames_cfg,
    input  wire         cic_cfg_valid,
    output wire         cic_cfg_busy,

    // RFADC tile 0: m00 is I and m01 is Q.
    input  wire [63:0]  m00_axis_tdata,
    input  wire         m00_axis_tvalid,
    output wire         m00_axis_tready,
    input  wire [63:0]  m01_axis_tdata,
    input  wire         m01_axis_tvalid,
    output wire         m01_axis_tready,

    // RFADC tile 1: m10 is I and m11 is Q.
    input  wire [63:0]  m10_axis_tdata,
    input  wire         m10_axis_tvalid,
    output wire         m10_axis_tready,
    input  wire [63:0]  m11_axis_tdata,
    input  wire         m11_axis_tvalid,
    output wire         m11_axis_tready,

    // FFT output to the AXI4-Stream FIFO/DMA path.
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
// Mode-change reset generation
// -------------------------------------------------------------------------
// Changing source while a frame is being assembled would mix samples from two
// sources. A mode change therefore resets both sides of the frame buffer and
// the r_clk-domain filters/FFT before the new source is accepted.
reg [3:0] dma_mode_r_d;
reg [7:0] mode_reset_r_count;

always @(posedge r_clk or negedge r_rst) begin
    if (!r_rst) begin
        dma_mode_r_d      <= MODE_IDLE;
        mode_reset_r_count <= 8'd0;
    end else begin
        dma_mode_r_d <= dma_mode;
        if (dma_mode != dma_mode_r_d)
            mode_reset_r_count <= MODE_RESET_R_CYCLES[7:0];
        else if (mode_reset_r_count != 0)
            mode_reset_r_count <= mode_reset_r_count - 1'b1;
    end
end

reg [3:0] dma_mode_w_meta;
reg [3:0] dma_mode_w_sync;
reg [3:0] dma_mode_w_d;
reg [7:0] mode_reset_w_count;

always @(posedge w_clk or negedge w_rst) begin
    if (!w_rst) begin
        dma_mode_w_meta    <= MODE_IDLE;
        dma_mode_w_sync    <= MODE_IDLE;
        dma_mode_w_d       <= MODE_IDLE;
        mode_reset_w_count <= 8'd0;
    end else begin
        dma_mode_w_meta <= dma_mode;
        dma_mode_w_sync <= dma_mode_w_meta;
        dma_mode_w_d    <= dma_mode_w_sync;

        if (dma_mode_w_sync != dma_mode_w_d)
            mode_reset_w_count <= MODE_RESET_W_CYCLES[7:0];
        else if (mode_reset_w_count != 0)
            mode_reset_w_count <= mode_reset_w_count - 1'b1;
    end
end

wire mode_reset_r = (mode_reset_r_count != 0);
wire mode_reset_w = (mode_reset_w_count != 0);
wire datapath_r_rst_n = r_rst && !mode_reset_r;
wire datapath_w_rst_n = w_rst && !mode_reset_w;
wire source_enabled = (dma_mode == MODE_DDS) ||
                      (dma_mode == MODE_ADC0) ||
                      (dma_mode == MODE_ADC1);

// Keep the RFADC output interfaces draining continuously. During idle, reset,
// or when another source is selected, their samples are intentionally dropped.
assign m00_axis_tready = r_rst;
assign m01_axis_tready = r_rst;
assign m10_axis_tready = r_rst;
assign m11_axis_tready = r_rst;

// -------------------------------------------------------------------------
// Internal DDS source
// -------------------------------------------------------------------------
wire signed [13:0] dds_i0;
wire signed [13:0] dds_i1;
wire signed [13:0] dds_i2;
wire signed [13:0] dds_i3;
wire signed [13:0] dds_q0;
wire signed [13:0] dds_q1;
wire signed [13:0] dds_q2;
wire signed [13:0] dds_q3;
wire               dds_valid;

active_signal u_active_signal (
    .sys_clk          (r_clk),
    .sys_rst          (!datapath_r_rst_n),
    .dds_en           (dma_mode == MODE_DDS),
    .resync           (dds_resync),
    .phase_step       ({dds_phase_step_h, dds_phase_step_l}),
    .dds_signal_i0    (dds_i0),
    .dds_signal_i1    (dds_i1),
    .dds_signal_i2    (dds_i2),
    .dds_signal_i3    (dds_i3),
    .dds_signal_q0    (dds_q0),
    .dds_signal_q1    (dds_q1),
    .dds_signal_q2    (dds_q2),
    .dds_signal_q3    (dds_q3),
    .dds_signal_valid (dds_valid)
);

// -------------------------------------------------------------------------
// RFADC 16-bit-word to signed 14-bit-sample conversion
// -------------------------------------------------------------------------
wire signed [13:0] adc0_i0 = $signed(m00_axis_tdata[15:2]);
wire signed [13:0] adc0_i1 = $signed(m00_axis_tdata[31:18]);
wire signed [13:0] adc0_i2 = $signed(m00_axis_tdata[47:34]);
wire signed [13:0] adc0_i3 = $signed(m00_axis_tdata[63:50]);
wire signed [13:0] adc0_q0 = $signed(m01_axis_tdata[15:2]);
wire signed [13:0] adc0_q1 = $signed(m01_axis_tdata[31:18]);
wire signed [13:0] adc0_q2 = $signed(m01_axis_tdata[47:34]);
wire signed [13:0] adc0_q3 = $signed(m01_axis_tdata[63:50]);

wire signed [13:0] adc1_i0 = $signed(m10_axis_tdata[15:2]);
wire signed [13:0] adc1_i1 = $signed(m10_axis_tdata[31:18]);
wire signed [13:0] adc1_i2 = $signed(m10_axis_tdata[47:34]);
wire signed [13:0] adc1_i3 = $signed(m10_axis_tdata[63:50]);
wire signed [13:0] adc1_q0 = $signed(m11_axis_tdata[15:2]);
wire signed [13:0] adc1_q1 = $signed(m11_axis_tdata[31:18]);
wire signed [13:0] adc1_q2 = $signed(m11_axis_tdata[47:34]);
wire signed [13:0] adc1_q3 = $signed(m11_axis_tdata[63:50]);

wire adc0_valid = m00_axis_tvalid && m01_axis_tvalid;
wire adc1_valid = m10_axis_tvalid && m11_axis_tvalid;

// Useful internal debug indicators. A high value means the RFADC I/Q valid
// signals did not arrive together for that tile.
wire adc0_valid_mismatch = m00_axis_tvalid ^ m01_axis_tvalid;
wire adc1_valid_mismatch = m10_axis_tvalid ^ m11_axis_tvalid;

// -------------------------------------------------------------------------
// Input source selection
// -------------------------------------------------------------------------
reg signed [13:0] source_i0;
reg signed [13:0] source_i1;
reg signed [13:0] source_i2;
reg signed [13:0] source_i3;
reg signed [13:0] source_q0;
reg signed [13:0] source_q1;
reg signed [13:0] source_q2;
reg signed [13:0] source_q3;
reg               source_valid;

always @(*) begin
    source_i0    = 14'sd0;
    source_i1    = 14'sd0;
    source_i2    = 14'sd0;
    source_i3    = 14'sd0;
    source_q0    = 14'sd0;
    source_q1    = 14'sd0;
    source_q2    = 14'sd0;
    source_q3    = 14'sd0;
    source_valid = 1'b0;

    case (dma_mode)
        MODE_DDS: begin
            source_i0    = dds_i0;
            source_i1    = dds_i1;
            source_i2    = dds_i2;
            source_i3    = dds_i3;
            source_q0    = dds_q0;
            source_q1    = dds_q1;
            source_q2    = dds_q2;
            source_q3    = dds_q3;
            source_valid = dds_valid;
        end

        MODE_ADC0: begin
            source_i0    = adc0_i0;
            source_i1    = adc0_i1;
            source_i2    = adc0_i2;
            source_i3    = adc0_i3;
            source_q0    = adc0_q0;
            source_q1    = adc0_q1;
            source_q2    = adc0_q2;
            source_q3    = adc0_q3;
            source_valid = adc0_valid;
        end

        MODE_ADC1: begin
            source_i0    = adc1_i0;
            source_i1    = adc1_i1;
            source_i2    = adc1_i2;
            source_i3    = adc1_i3;
            source_q0    = adc1_q0;
            source_q1    = adc1_q1;
            source_q2    = adc1_q2;
            source_q3    = adc1_q3;
            source_valid = adc1_valid;
        end

        default: begin
            source_i0    = 14'sd0;
            source_i1    = 14'sd0;
            source_i2    = 14'sd0;
            source_i3    = 14'sd0;
            source_q0    = 14'sd0;
            source_q1    = 14'sd0;
            source_q2    = 14'sd0;
            source_q3    = 14'sd0;
            source_valid = 1'b0;
        end
    endcase
end

// -------------------------------------------------------------------------
// Decimation filters
// -------------------------------------------------------------------------
wire [31:0] data_out_time_i;
wire [31:0] data_out_time_q;
wire        data_out_time_valid_i;
wire        data_out_time_valid_q;
wire        cic_cfg_busy_i;
wire        cic_cfg_busy_q;

// 两路滤波器都空闲时才接受同一个配置脉冲，保证I/Q参数同步生效。
wire cic_cfg_valid_i = cic_cfg_valid && !cic_cfg_busy;
assign cic_cfg_busy = cic_cfg_busy_i | cic_cfg_busy_q;

dec_filter_bank u_dec_filter_bank_i (
    .clk            (r_clk),
    .rst_n          (datapath_r_rst_n),
    .ch0_data       (source_i0),
    .ch1_data       (source_i1),
    .ch2_data       (source_i2),
    .ch3_data       (source_i3),
    .data_in_valid  (source_valid && datapath_r_rst_n),
    .cic_rate_cfg   (cic_rate_cfg),
    .cic_trunc_cfg  (cic_trunc_cfg),
    .cic_cfg_valid  (cic_cfg_valid_i),
    .cic_cfg_busy   (cic_cfg_busy_i),
    .hb_level       (hb_level),
    .data_out       (data_out_time_i),
    .data_out_valid (data_out_time_valid_i)
);

dec_filter_bank u_dec_filter_bank_q (
    .clk            (r_clk),
    .rst_n          (datapath_r_rst_n),
    .ch0_data       (source_q0),
    .ch1_data       (source_q1),
    .ch2_data       (source_q2),
    .ch3_data       (source_q3),
    .data_in_valid  (source_valid && datapath_r_rst_n),
    .cic_rate_cfg   (cic_rate_cfg),
    .cic_trunc_cfg  (cic_trunc_cfg),
    .cic_cfg_valid  (cic_cfg_valid_i),
    .cic_cfg_busy   (cic_cfg_busy_q),
    .hb_level       (hb_level),
    .data_out       (data_out_time_q),
    .data_out_valid (data_out_time_valid_q)
);

wire        data_out_time_valid = data_out_time_valid_i &&
                                  data_out_time_valid_q;
wire [63:0] data_out_dec = {data_out_time_q, data_out_time_i};

// -------------------------------------------------------------------------
// 重叠组帧、Blackman-Harris加窗和FFT
// -------------------------------------------------------------------------
wire [63:0] data_out_frame;
wire        data_out_frame_valid;

// 反压按整帧处理。只有FFT输入FIFO能够容纳完整的1024点数据时，
// 帧控制模块才允许启动新的一帧。
(* mark_debug = "true" *) wire        fft_frame_can_accept;
(* mark_debug = "true" *) wire        frame_drop_pulse;
(* mark_debug = "true" *) wire [31:0] frame_drop_count;
(* mark_debug = "true" *) wire        fft_fifo_overflow_sticky;

frame_ctrl_v3 u_frame_ctrl (
    // 抽取倍率可变时不能再用固定的312.5 MHz/32时钟采样滤波结果。
    // FIFO写侧与抽取滤波器统一使用r_clk，data_out_time_valid作为写使能。
    .w_rst             (!datapath_r_rst_n),
    .w_clk             (r_clk),
    .r_rst             (!datapath_r_rst_n),
    .r_clk             (r_clk),
    .frame_width_reg   (16'd1024),
    .overlap_ratio_reg (16'd768),
    .in_valid          (data_out_time_valid && source_enabled &&
                        datapath_r_rst_n),
    .data_in           (data_out_dec),
    .frame_can_accept  (fft_frame_can_accept),
    .out_valid         (data_out_frame_valid),
    .data_out          (data_out_frame),
    .frame_drop_pulse  (frame_drop_pulse),
    .frame_drop_count  (frame_drop_count)
);

// -------------------------------------------------------------------------
// 1024点四项Blackman-Harris加窗
//
// 加窗放在重叠组帧之后，使同一个原始采样点出现在不同重叠帧位置时，
// 能够分别乘以该帧对应位置的窗系数。window_rom内部吞吐率为每拍1点，
// 只增加固定流水线延迟，不改变每帧1024点的数据长度。
// -------------------------------------------------------------------------
wire [63:0] data_out_window;
wire        data_out_window_valid;
wire        data_out_window_last;

window_rom u_window_rom (
    .clk            (r_clk),
    .rst            (!datapath_r_rst_n),
    .data_in        (data_out_frame),
    .data_in_valid  (data_out_frame_valid),
    .data_out       (data_out_window),
    .data_out_valid (data_out_window_valid),
    .data_out_last  (data_out_window_last)
);

wire        fft_result_wen;
wire [63:0] fft_result_data;
wire [63:0] fft_raw_tdata;
wire        fft_raw_tvalid;
wire        fft_raw_tready;
wire        fft_raw_tlast;

xfft_1024 #(
    .FFT_POINTS (16'd1024)
) u_xfft_1024 (
    .rst                (!datapath_r_rst_n),
    .clk                (r_clk),
    .overlap_wen        (data_out_window_valid),
    .overlap_data       (data_out_window),
    .m_axis_data_tready (fft_raw_tready),
    .fft_result_wen     (fft_result_wen),
    .fft_result_data    (fft_result_data),
    .m_axis_data_tdata  (fft_raw_tdata),
    .m_axis_data_tvalid (fft_raw_tvalid),
    .m_axis_data_tlast  (fft_raw_tlast),
    .frame_can_accept   (fft_frame_can_accept),
    .fifo_overflow_sticky (fft_fifo_overflow_sticky)
);

// -------------------------------------------------------------------------
// 逐频点最大保持
//
// 累计帧数由PS配置，配置值在每组最大保持开始时锁存。
// 写0时最大保持模块按1帧处理。
// 比较实部平方加虚部平方，并保留最大功率对应的完整复数FFT数据。
// -------------------------------------------------------------------------
fft_max_hold #(
    .FFT_POINTS (1024)
) u_fft_max_hold (
    .clk           (r_clk),
    .rst           (!datapath_r_rst_n),
    .s_axis_tdata  (fft_raw_tdata),
    .s_axis_tvalid (fft_raw_tvalid),
    .s_axis_tready (fft_raw_tready),
    .s_axis_tlast  (fft_raw_tlast),
    .hold_frames_cfg (max_hold_frames_cfg),
    .m_axis_tdata  (m_axis_data_tdata),
    .m_axis_tvalid (m_axis_data_tvalid),
    .m_axis_tready (m_axis_data_tready),
    .m_axis_tlast  (m_axis_data_tlast)
);

// -------------------------------------------------------------------------
// ILA继续观察所选输入源。probe9表示RFADC的I/Q有效信号是否错位，
// probe10直接观察送入组帧FIFO的抽取滤波输出有效脉冲。
// -------------------------------------------------------------------------
wire rfdc_valid_mismatch = adc0_valid_mismatch | adc1_valid_mismatch;

ila_overlap_frame u_ila_overlap_frame (
    .clk     (r_clk),
    .probe0  (source_i0),
    .probe1  (source_i1),
    .probe2  (source_i2),
    .probe3  (source_i3),
    .probe4  (source_q0),
    .probe5  (source_q1),
    .probe6  (source_q2),
    .probe7  (source_q3),
    .probe8  (data_out_dec),
    .probe9  (rfdc_valid_mismatch),
    .probe10 (data_out_time_valid),
    .probe11 (data_out_frame),
    .probe12 (data_out_frame_valid),
    .probe13 (m_axis_data_tdata),
    .probe14 (m_axis_data_tready),
    .probe15 (m_axis_data_tvalid),
    .probe16 (m_axis_data_tlast)
);

endmodule
