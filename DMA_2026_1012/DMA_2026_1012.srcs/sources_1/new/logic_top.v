`timescale 1ns / 1ps

// =============================================================================
// 模块名称：logic_top
// 功能说明：FPGA业务逻辑总封装。
//
// 内部包含：
// 1. reg_ctrl：接收PS寄存器配置，并向PS返回运行状态；
// 2. spectrum_engine：完成抽取、重叠帧、加窗、FFT、最大保持和触发捕获；
// 3. DMA输出选择：选择实时频谱数据或触发后的时域IQ数据送往DMA。
// =============================================================================

module logic_top (
    // 业务数据通路时钟和复位。
    // logic_clk对应频谱处理模块的r_clk，r_rst和w_rst均为低有效复位。
    input  wire         logic_clk,
    input  wire         r_rst,
    input  wire         w_clk,
    input  wire         w_rst,

    // AXI BRAM寄存器接口。
    input  wire [12:0]  axi_bram_addr,
    input  wire         axi_bram_clk,
    input  wire [31:0]  axi_bram_wd_din,
    output wire [31:0]  axi_bram_rd_dout,
    input  wire         axi_bram_en,
    input  wire         axi_bram_rst,
    input  wire [3:0]   axi_bram_we,

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

    // 送往AXI DMA的统一输出接口。
    input  wire         m_axis_data_tready,
    output wire [63:0]  m_axis_data_tdata,
    output wire [7:0]   m_axis_data_tkeep,
    output wire         m_axis_data_tvalid,
    output wire         m_axis_data_tlast,

    // 保留的通用寄存器访问接口。
    output wire         reg_clk,
    output wire [11:0]  reg_addr,
    output wire         reg_wr_en,
    output wire         reg_rd_en,
    output wire [15:0]  reg_data
);

// -----------------------------------------------------------------------------
// PS下发给频谱处理模块的配置。
// -----------------------------------------------------------------------------
wire [3:0]  dma_mode;
wire [15:0] dds_phase_step_l;
wire [15:0] dds_phase_step_h;
wire [15:0] cic_rate_cfg;
wire [7:0]  cic_trunc_cfg;
wire [2:0]  hb_level;
wire [15:0] max_hold_frames_cfg;

wire        trigger_enable;
wire        capture_arm;
wire        trigger_clear;
wire        dma_output_select;
wire [9:0]  trigger_start_bin;
wire [9:0]  trigger_end_bin;
wire [63:0] trigger_power_threshold;
wire [31:0] pre_sample_num;
wire [31:0] post_sample_num;

wire [63:0] cfg_center_freq_hz;
wire [63:0] cfg_span_hz;
wire [7:0]  cfg_trace_mode;
wire [15:0] cfg_accum_frames;
wire [7:0]  cfg_window_type;
wire [31:0] cfg_cal_offset_q16_16;
wire [15:0] cfg_config_epoch;

// Frame-synchronized active configuration. The PS-visible wires above are
// shadow values; only pl_config_latch drives the live datapath.
wire [3:0]  active_dma_mode;
wire [31:0] active_dds_phase_step;
wire        active_dds_resync;
wire [15:0] active_cic_rate_cfg;
wire [7:0]  active_cic_trunc_cfg;
wire        active_cic_cfg_valid;
wire [2:0]  active_hb_level;
wire [15:0] active_max_hold_frames_cfg;
wire [63:0] active_cfg_center_freq_hz;
wire [63:0] active_cfg_span_hz;
wire [7:0]  active_cfg_trace_mode;
wire [15:0] active_cfg_accum_frames;
wire [7:0]  active_cfg_window_type;
wire [31:0] active_cfg_cal_offset_q16_16;
wire [15:0] active_cfg_config_epoch;
wire        active_cfg_hold;
wire        active_datapath_reset;
wire        config_commit_pulse;
wire        config_busy;
wire        frame_idle;
wire        frame_complete;

// -----------------------------------------------------------------------------
// 频谱处理模块返回给PS读取的状态。
// -----------------------------------------------------------------------------
wire        cic_cfg_busy;
wire        trigger_latched;
wire [9:0]  trigger_bin;
wire        capture_ready;
wire        capture_busy;
wire        capture_triggered;
wire        capture_done;

// 以下状态暂时不映射到PS寄存器，但保留为内部调试信号。
wire        trigger_pulse;
wire [63:0] trigger_power;
wire [63:0] trigger_threshold;
wire [31:0] captured_sample_num;

// -----------------------------------------------------------------------------
// 实时频谱和触发时域数据的两路AXI4-Stream接口。
// -----------------------------------------------------------------------------
wire [63:0] spectrum_axis_tdata;
wire        spectrum_axis_tvalid;
wire        spectrum_axis_tready;
wire        spectrum_axis_tlast;

wire [63:0] spectrum_frame_tdata;
wire [7:0]  spectrum_frame_tkeep;
wire        spectrum_frame_tvalid;
wire        spectrum_frame_tready;
wire        spectrum_frame_tlast;

wire [63:0] trigger_axis_tdata;
wire [7:0]  trigger_axis_tkeep;
wire        trigger_axis_tvalid;
wire        trigger_axis_tready;
wire        trigger_axis_tlast;

// -----------------------------------------------------------------------------
// PS寄存器控制。
// -----------------------------------------------------------------------------
reg_ctrl u_reg_ctrl (
    .logic_clk              (logic_clk),

    .axi_bram_addr          (axi_bram_addr),
    .axi_bram_clk           (axi_bram_clk),
    .axi_bram_wd_din        (axi_bram_wd_din),
    .axi_bram_rd_dout       (axi_bram_rd_dout),
    .axi_bram_en            (axi_bram_en),
    .axi_bram_rst           (axi_bram_rst),
    .axi_bram_we            (axi_bram_we),

    .cic_cfg_busy           (cic_cfg_busy),
    .trigger_latched        (trigger_latched),
    .trigger_bin            (trigger_bin),
    .capture_ready          (capture_ready),
    .capture_busy           (capture_busy),
    .capture_triggered      (capture_triggered),
    .capture_done           (capture_done),
    .config_busy            (config_busy),
    .active_config_epoch    (active_cfg_config_epoch),

    .reg_clk                (reg_clk),
    .reg_addr               (reg_addr),
    .reg_wr_en              (reg_wr_en),
    .reg_rd_en              (reg_rd_en),
    .reg_data               (reg_data),

    .dma_mode               (dma_mode),
    .dds_phase_step_l       (dds_phase_step_l),
    .dds_phase_step_h       (dds_phase_step_h),
    .cic_rate_cfg           (cic_rate_cfg),
    .cic_trunc_cfg          (cic_trunc_cfg),
    .hb_level               (hb_level),
    .max_hold_frames_cfg    (max_hold_frames_cfg),

    .trigger_enable         (trigger_enable),
    .capture_arm            (capture_arm),
    .trigger_clear          (trigger_clear),
    .dma_output_select      (dma_output_select),
    .trigger_start_bin      (trigger_start_bin),
    .trigger_end_bin        (trigger_end_bin),
    .trigger_power_threshold(trigger_power_threshold),
    .pre_sample_num         (pre_sample_num),
    .post_sample_num        (post_sample_num),
    .cfg_center_freq_hz     (cfg_center_freq_hz),
    .cfg_span_hz            (cfg_span_hz),
    .cfg_trace_mode         (cfg_trace_mode),
    .cfg_accum_frames       (cfg_accum_frames),
    .cfg_window_type        (cfg_window_type),
    .cfg_cal_offset_q16_16  (cfg_cal_offset_q16_16),
    .cfg_config_epoch       (cfg_config_epoch),
    .config_commit_pulse    (config_commit_pulse)
);

// -----------------------------------------------------------------------------
// Shadow-to-active frame-boundary configuration latch.
// The AXI DMA and descriptor ring are intentionally outside this sequencer.
// -----------------------------------------------------------------------------
pl_config_latch u_pl_config_latch (
    .clk                        (logic_clk),
    .rst                        (!r_rst),

    .commit_pulse               (config_commit_pulse),
    .frame_idle                 (frame_idle),
    .frame_complete             (frame_complete),
    .cic_busy                   (cic_cfg_busy),

    .shadow_center_freq_hz      (cfg_center_freq_hz),
    .shadow_span_hz             (cfg_span_hz),
    .shadow_trace_mode          (cfg_trace_mode),
    .shadow_accum_frames        (cfg_accum_frames),
    .shadow_window_type         (cfg_window_type),
    .shadow_cal_offset_q16_16   (cfg_cal_offset_q16_16),
    .shadow_config_epoch        (cfg_config_epoch),
    .shadow_dma_mode            (dma_mode),
    .shadow_dds_phase_step      ({dds_phase_step_h, dds_phase_step_l}),
    .shadow_cic_rate            (cic_rate_cfg),
    .shadow_cic_trunc           (cic_trunc_cfg),
    .shadow_hb_level            (hb_level),
    .shadow_max_hold_frames     (max_hold_frames_cfg),

    .active_center_freq_hz      (active_cfg_center_freq_hz),
    .active_span_hz             (active_cfg_span_hz),
    .active_trace_mode          (active_cfg_trace_mode),
    .active_accum_frames        (active_cfg_accum_frames),
    .active_window_type         (active_cfg_window_type),
    .active_cal_offset_q16_16   (active_cfg_cal_offset_q16_16),
    .active_config_epoch        (active_cfg_config_epoch),
    .active_dma_mode            (active_dma_mode),
    .active_dds_phase_step      (active_dds_phase_step),
    .active_cic_rate            (active_cic_rate_cfg),
    .active_cic_trunc           (active_cic_trunc_cfg),
    .active_hb_level            (active_hb_level),
    .active_max_hold_frames     (active_max_hold_frames_cfg),

    .cfg_hold                   (active_cfg_hold),
    .datapath_reset             (active_datapath_reset),
    .cic_cfg_valid              (active_cic_cfg_valid),
    .dds_resync                 (active_dds_resync),
    .config_busy                (config_busy)
);

// -----------------------------------------------------------------------------
// 频谱处理和触发捕获。
// -----------------------------------------------------------------------------
spectrum_engine u_spectrum_engine (
    .w_rst                     (w_rst),
    .w_clk                     (w_clk),
    .r_rst                     (r_rst),
    .r_clk                     (logic_clk),

    .dma_mode                  (active_dma_mode),
    .dds_resync                (active_dds_resync),
    .dds_phase_step_h          (active_dds_phase_step[31:16]),
    .dds_phase_step_l          (active_dds_phase_step[15:0]),

    .cic_rate_cfg              (active_cic_rate_cfg),
    .cic_trunc_cfg             (active_cic_trunc_cfg),
    .hb_level                  (active_hb_level),
    .max_hold_frames_cfg       (active_max_hold_frames_cfg),
    .cic_cfg_valid             (active_cic_cfg_valid),
    .cic_cfg_busy              (cic_cfg_busy),
    .config_datapath_reset     (active_datapath_reset),

    .m00_axis_tdata            (m00_axis_tdata),
    .m00_axis_tvalid           (m00_axis_tvalid),
    .m00_axis_tready           (m00_axis_tready),
    .m01_axis_tdata            (m01_axis_tdata),
    .m01_axis_tvalid           (m01_axis_tvalid),
    .m01_axis_tready           (m01_axis_tready),

    .m10_axis_tdata            (m10_axis_tdata),
    .m10_axis_tvalid           (m10_axis_tvalid),
    .m10_axis_tready           (m10_axis_tready),
    .m11_axis_tdata            (m11_axis_tdata),
    .m11_axis_tvalid           (m11_axis_tvalid),
    .m11_axis_tready           (m11_axis_tready),

    .trigger_enable            (trigger_enable),
    .trigger_clear             (trigger_clear),
    .trigger_start_bin         (trigger_start_bin),
    .trigger_end_bin           (trigger_end_bin),
    .trigger_power_threshold   (trigger_power_threshold),

    .capture_arm               (capture_arm),
    // 一个清除脉冲同时清除频率触发锁存和时域捕获状态。
    .capture_clear             (trigger_clear),
    .pre_sample_num            (pre_sample_num),
    .post_sample_num           (post_sample_num),

    .trigger_pulse             (trigger_pulse),
    .trigger_latched           (trigger_latched),
    .trigger_bin               (trigger_bin),
    .trigger_power             (trigger_power),
    .trigger_threshold         (trigger_threshold),
    .capture_busy              (capture_busy),
    .capture_ready             (capture_ready),
    .capture_triggered         (capture_triggered),
    .capture_done              (capture_done),
    .captured_sample_num       (captured_sample_num),

    .trigger_axis_tready       (trigger_axis_tready),
    .trigger_axis_tdata        (trigger_axis_tdata),
    .trigger_axis_tkeep        (trigger_axis_tkeep),
    .trigger_axis_tvalid       (trigger_axis_tvalid),
    .trigger_axis_tlast        (trigger_axis_tlast),

    .m_axis_data_tready        (spectrum_axis_tready),
    .m_axis_data_tdata         (spectrum_axis_tdata),
    .m_axis_data_tvalid        (spectrum_axis_tvalid),
    .m_axis_data_tlast         (spectrum_axis_tlast)
);

// -----------------------------------------------------------------------------
// 频谱帧格式化：64 B header + int16 对数幅度 + TLAST
// -----------------------------------------------------------------------------
spectrum_frame_builder #(
    .BIN_COUNT (1024)
) u_spectrum_frame_builder (
    .clk            (logic_clk),
    .rst            (!r_rst),
    .cfg_center_freq_hz(active_cfg_center_freq_hz),
    .cfg_span_hz(active_cfg_span_hz),
    .cfg_trace_mode(active_cfg_trace_mode),
    .cfg_accum_frames(active_cfg_accum_frames),
    .cfg_window_type(active_cfg_window_type),
    .cfg_cal_offset_q16_16(active_cfg_cal_offset_q16_16),
    .cfg_config_epoch(active_cfg_config_epoch),
    .cfg_hold       (active_cfg_hold),
    .s_axis_tdata   (spectrum_axis_tdata),
    .s_axis_tvalid  (spectrum_axis_tvalid),
    .s_axis_tready  (spectrum_axis_tready),
    .s_axis_tlast   (spectrum_axis_tlast),
    .m_axis_tdata   (spectrum_frame_tdata),
    .m_axis_tkeep   (spectrum_frame_tkeep),
    .m_axis_tvalid  (spectrum_frame_tvalid),
    .m_axis_tready  (spectrum_frame_tready),
    .m_axis_tlast   (spectrum_frame_tlast),
    .frame_idle     (frame_idle),
    .frame_complete (frame_complete)
);

// -----------------------------------------------------------------------------
// DMA输出选择。
// dma_output_select=0：输出最大保持后的实时频谱；
// dma_output_select=1：输出触发前后的时域IQ数据。
//
// tready只返回给当前被选择的数据源，未被选择的数据源会保持等待。
// 软件应在一包数据传输开始前切换选择，不能在一包中途改变该配置。
// -----------------------------------------------------------------------------
assign m_axis_data_tdata  = dma_output_select ? trigger_axis_tdata  : spectrum_frame_tdata;
assign m_axis_data_tkeep  = dma_output_select ? trigger_axis_tkeep  : spectrum_frame_tkeep;
assign m_axis_data_tvalid = dma_output_select ? trigger_axis_tvalid : spectrum_frame_tvalid;
assign m_axis_data_tlast  = dma_output_select ? trigger_axis_tlast  : spectrum_frame_tlast;

assign trigger_axis_tready  = dma_output_select  ? m_axis_data_tready : 1'b0;
assign spectrum_frame_tready = !dma_output_select ? m_axis_data_tready : 1'b0;

endmodule
