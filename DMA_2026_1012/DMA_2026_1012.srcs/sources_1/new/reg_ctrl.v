`timescale 1ns / 1ps

module reg_ctrl (
    input  wire         logic_clk,

    input  wire [12:0]  axi_bram_addr,
    input  wire         axi_bram_clk,
    input  wire [31:0]  axi_bram_wd_din,
    output wire [31:0]  axi_bram_rd_dout,
    input  wire         axi_bram_en,
    input  wire         axi_bram_rst,
    input  wire [3:0]   axi_bram_we,

    input  wire         cic_cfg_busy,

    // 频率触发和时域捕获状态。
    input  wire         trigger_latched,
    input  wire [9:0]   trigger_bin,
    input  wire         capture_ready,
    input  wire         capture_busy,
    input  wire         capture_triggered,
    input  wire         capture_done,
    input  wire         config_busy,
    input  wire [15:0]  active_config_epoch,

    output wire         reg_clk,
    output wire [11:0]  reg_addr,
    output wire         reg_wr_en,
    output wire         reg_rd_en,
    output wire [15:0]  reg_data,

    output wire [15:0]  dds_phase_step_h,
    output wire [15:0]  dds_phase_step_l,
    output wire [3:0]   dma_mode,
    output wire [15:0]  cic_rate_cfg,
    output wire [7:0]   cic_trunc_cfg,
    output wire [2:0]   hb_level,
    output wire [15:0]  max_hold_frames_cfg,

    // 精简后的频率触发配置。
    output wire         trigger_enable,
    output wire         capture_arm,
    output wire         trigger_clear,
    output wire         dma_output_select,
    output wire [9:0]   trigger_start_bin,
    output wire [9:0]   trigger_end_bin,
    output wire [63:0]  trigger_power_threshold,
    output wire [31:0]  pre_sample_num,
    output wire [31:0]  post_sample_num,

    output wire [63:0]  cfg_center_freq_hz,
    output wire [63:0]  cfg_span_hz,
    output wire [7:0]   cfg_trace_mode,
    output wire [15:0]  cfg_accum_frames,
    output wire [7:0]   cfg_window_type,
    output wire [31:0]  cfg_cal_offset_q16_16,
    output wire [15:0]  cfg_config_epoch,
    output wire         config_commit_pulse
);

wire        latch_wr_en;
wire        latch_rd_en;
wire [12:0] latch_addr;
wire [31:0] latch_wdata;

bram_ctrl_intf u_bram_ctrl_intf (
    .bram_0_clk  (axi_bram_clk),
    .bram_0_en   (axi_bram_en),
    .bram_0_we   (axi_bram_we),
    .bram_0_addr (axi_bram_addr),
    .bram_0_din  (axi_bram_wd_din),
    .latch_addr  (latch_addr),
    .latch_wdata (latch_wdata),
    .latch_wr_en (latch_wr_en),
    .latch_rd_en (latch_rd_en)
);

axi_bram_ctrl u_axi_bram_ctrl (
    .logic_clk           (logic_clk),
    .axi_bram_addr       (latch_addr),
    .axi_bram_clk        (axi_bram_clk),
    .axi_bram_wd_din     (latch_wdata),
    .axi_bram_rd_dout    (axi_bram_rd_dout),
    .axi_bram_rst        (axi_bram_rst),
    .axi_bram_wr_en      (latch_wr_en),
    .axi_bram_rd_en      (latch_rd_en),
    .cic_cfg_busy        (cic_cfg_busy),
    .trigger_latched     (trigger_latched),
    .trigger_bin         (trigger_bin),
    .capture_ready       (capture_ready),
    .capture_busy        (capture_busy),
    .capture_triggered   (capture_triggered),
    .capture_done        (capture_done),
    .config_busy         (config_busy),
    .active_config_epoch (active_config_epoch),
    .dds_phase_step_h    (dds_phase_step_h),
    .dds_phase_step_l    (dds_phase_step_l),
    .dma_mode            (dma_mode),
    .cic_rate_cfg        (cic_rate_cfg),
    .cic_trunc_cfg       (cic_trunc_cfg),
    .hb_level            (hb_level),
    .max_hold_frames_cfg (max_hold_frames_cfg),
    .trigger_enable      (trigger_enable),
    .capture_arm         (capture_arm),
    .trigger_clear       (trigger_clear),
    .dma_output_select   (dma_output_select),
    .trigger_start_bin   (trigger_start_bin),
    .trigger_end_bin     (trigger_end_bin),
    .trigger_power_threshold(trigger_power_threshold),
    .pre_sample_num      (pre_sample_num),
    .post_sample_num     (post_sample_num),
    .cfg_center_freq_hz  (cfg_center_freq_hz),
    .cfg_span_hz         (cfg_span_hz),
    .cfg_trace_mode      (cfg_trace_mode),
    .cfg_accum_frames    (cfg_accum_frames),
    .cfg_window_type     (cfg_window_type),
    .cfg_cal_offset_q16_16(cfg_cal_offset_q16_16),
    .cfg_config_epoch    (cfg_config_epoch),
    .config_commit_pulse (config_commit_pulse)
);

assign reg_clk   = axi_bram_clk;
assign reg_addr  = latch_addr[11:0];
assign reg_wr_en = latch_addr[12] ? latch_wr_en : 1'b0;
assign reg_rd_en = latch_addr[12] ? latch_rd_en : 1'b0;
assign reg_data  = latch_wdata[15:0];

endmodule
