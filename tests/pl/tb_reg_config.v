`timescale 1ns / 1ps

// Standalone register-map test. The ILA instance inside bram_ctrl_intf is
// stubbed here because this test only exercises the register decode path.
module ila_bram_ctrl_intf (
    input wire clk,
    input wire probe0,
    input wire probe1,
    input wire [12:0] probe2,
    input wire [31:0] probe3
);
endmodule

module tb_reg_config;

reg logic_clk = 0;
reg axi_bram_clk = 0;
reg axi_bram_rst = 1;
reg [12:0] axi_bram_addr = 0;
reg [31:0] axi_bram_wd_din = 0;
reg axi_bram_en = 0;
reg [3:0] axi_bram_we = 0;
reg config_busy_in = 0;
reg [15:0] active_epoch_in = 0;

wire [31:0] axi_bram_rd_dout;
wire reg_clk;
wire [11:0] reg_addr;
wire reg_wr_en;
wire reg_rd_en;
wire [15:0] reg_data;

wire [63:0] cfg_center_freq_hz;
wire [63:0] cfg_span_hz;
wire [7:0] cfg_trace_mode;
wire [15:0] cfg_accum_frames;
wire [7:0] cfg_window_type;
wire [31:0] cfg_cal_offset_q16_16;
wire [15:0] cfg_config_epoch;
wire [15:0] dds_phase_step_h;
wire [15:0] dds_phase_step_l;
wire [3:0]  dma_mode;
wire [15:0] cic_rate_cfg;
wire [7:0]  cic_trunc_cfg;
wire [2:0]  hb_level;
wire [15:0] max_hold_frames_cfg;
wire        config_commit_pulse;

integer commit_pulses = 0;

always @(posedge logic_clk) begin
    if (config_commit_pulse)
        commit_pulses = commit_pulses + 1;
end

always #5 logic_clk = ~logic_clk;
always #5 axi_bram_clk = ~axi_bram_clk;

reg_ctrl dut (
    .logic_clk(logic_clk),
    .axi_bram_addr(axi_bram_addr),
    .axi_bram_clk(axi_bram_clk),
    .axi_bram_wd_din(axi_bram_wd_din),
    .axi_bram_rd_dout(axi_bram_rd_dout),
    .axi_bram_en(axi_bram_en),
    .axi_bram_rst(axi_bram_rst),
    .axi_bram_we(axi_bram_we),
    .cic_cfg_busy(1'b0),
    .trigger_latched(1'b0),
    .trigger_bin(10'd0),
    .capture_ready(1'b0),
    .capture_busy(1'b0),
    .capture_triggered(1'b0),
    .capture_done(1'b0),
    .config_busy(config_busy_in),
    .active_config_epoch(active_epoch_in),
    .reg_clk(reg_clk),
    .reg_addr(reg_addr),
    .reg_wr_en(reg_wr_en),
    .reg_rd_en(reg_rd_en),
    .reg_data(reg_data),
    .dds_phase_step_h(dds_phase_step_h),
    .dds_phase_step_l(dds_phase_step_l),
    .dma_mode(dma_mode),
    .cic_rate_cfg(cic_rate_cfg),
    .cic_trunc_cfg(cic_trunc_cfg),
    .hb_level(hb_level),
    .max_hold_frames_cfg(max_hold_frames_cfg),
    .trigger_enable(),
    .capture_arm(),
    .trigger_clear(),
    .dma_output_select(),
    .trigger_start_bin(),
    .trigger_end_bin(),
    .trigger_power_threshold(),
    .pre_sample_num(),
    .post_sample_num(),
    .cfg_center_freq_hz(cfg_center_freq_hz),
    .cfg_span_hz(cfg_span_hz),
    .cfg_trace_mode(cfg_trace_mode),
    .cfg_accum_frames(cfg_accum_frames),
    .cfg_window_type(cfg_window_type),
    .cfg_cal_offset_q16_16(cfg_cal_offset_q16_16),
    .cfg_config_epoch(cfg_config_epoch),
    .config_commit_pulse(config_commit_pulse)
);

task write_reg;
    input [12:0] addr;
    input [31:0] value;
    begin
        @(negedge axi_bram_clk);
        axi_bram_addr = addr;
        axi_bram_wd_din = value;
        axi_bram_en = 1'b1;
        axi_bram_we = 4'hF;
        @(negedge axi_bram_clk);
        axi_bram_en = 1'b0;
        axi_bram_we = 4'd0;
        repeat (4) @(negedge axi_bram_clk);
    end
endtask

task check_defaults;
    begin
        if (dma_mode !== 4'd1 || dds_phase_step_h !== 16'h004E
            || dds_phase_step_l !== 16'hA4A9
            || cic_rate_cfg !== 16'd8 || cic_trunc_cfg !== 8'd31
            || hb_level !== 3'd2 || max_hold_frames_cfg !== 16'd80
            || cfg_span_hz !== 64'd9765625 || cfg_trace_mode !== 8'd1
            || cfg_accum_frames !== 16'd80 || cfg_window_type !== 8'd4
            || cfg_config_epoch !== 16'd1) begin
            $error("power-on defaults mismatch");
            $fatal(1);
        end
    end
endtask

initial begin
    axi_bram_rst = 1'b1;
    repeat (5) @(negedge axi_bram_clk);
    axi_bram_rst = 1'b0;
    repeat (10) @(negedge logic_clk);
    check_defaults();

    // Staging writes must be visible, but must not produce a commit pulse.
    write_reg(13'h100, 32'd1000000000);
    write_reg(13'h104, 32'd1);
    write_reg(13'h108, 32'd200000000);
    write_reg(13'h10C, 32'd2);
    write_reg(13'h110, (32'd16 << 8) | 32'd2);
    write_reg(13'h114, 32'd1);
    write_reg(13'h118, 32'd12345);
    write_reg(13'h11C, 32'd7);
    write_reg(13'h200, 32'h004E);
    write_reg(13'h204, 32'hA4A9);
    write_reg(13'h20C, 32'd16);
    write_reg(13'h210, 32'd2);
    write_reg(13'h214, 32'd33);
    write_reg(13'h218, 32'd3);
    write_reg(13'h224, 32'd120);

    repeat (10) @(negedge logic_clk);
    if (commit_pulses != 0) begin
        $error("staging write produced %0d commit pulses", commit_pulses);
        $fatal(1);
    end
    // Staging writes must not leak into the committed bank.
    check_defaults();

    // Reserved command addresses must not reach the datapath.
    write_reg(13'h208, 32'd1);
    write_reg(13'h21C, 32'd1);
    repeat (10) @(negedge logic_clk);
    if (commit_pulses != 0) begin
        $error("reserved command write produced a commit pulse");
        $fatal(1);
    end

    // Commit produces exactly one pulse per 0->1 transaction.
    write_reg(13'h120, 32'd0);
    write_reg(13'h120, 32'd1);
    repeat (10) @(negedge logic_clk);
    if (commit_pulses != 1) begin
        $error("first commit produced %0d pulses", commit_pulses);
        $fatal(1);
    end
    if (cfg_center_freq_hz !== 64'd5294967296
        || cfg_span_hz !== 64'd8789934592
        || cfg_trace_mode !== 8'd2 || cfg_accum_frames !== 16'd16
        || cfg_window_type !== 8'd1 || cfg_config_epoch !== 16'd7) begin
        $error("committed frame fields mismatch");
        $display("  center=%0d span=%0d trace=%0d accum=%0d window=%0d epoch=%0d",
                 cfg_center_freq_hz, cfg_span_hz, cfg_trace_mode,
                 cfg_accum_frames, cfg_window_type, cfg_config_epoch);
        $fatal(1);
    end
    if (dma_mode !== 4'd2 || cic_rate_cfg !== 16'd16
        || cic_trunc_cfg !== 8'd33 || hb_level !== 3'd3
        || max_hold_frames_cfg !== 16'd120) begin
        $error("committed datapath fields mismatch");
        $fatal(1);
    end

    // Status readback is synchronized from the logic_clk domain.
    config_busy_in = 1'b1;
    active_epoch_in = 16'h0055;
    repeat (6) @(negedge axi_bram_clk);
    @(negedge axi_bram_clk);
    axi_bram_addr = 13'h130;
    axi_bram_en = 1'b1;
    axi_bram_we = 4'd0;
    repeat (5) @(negedge axi_bram_clk);
    if (axi_bram_rd_dout[0] !== 1'b1) begin
        $error("CONFIG_BUSY readback mismatch: %h", axi_bram_rd_dout);
        $fatal(1);
    end
    @(negedge axi_bram_clk);
    axi_bram_addr = 13'h134;
    repeat (5) @(negedge axi_bram_clk);
    if (axi_bram_rd_dout[15:0] !== 16'h0055) begin
        $error("ACTIVE_EPOCH readback mismatch: %h", axi_bram_rd_dout);
        $fatal(1);
    end
    axi_bram_en = 1'b0;
    config_busy_in = 1'b0;

    // A second commit must remain effective with the same level-based map.
    write_reg(13'h11C, 32'd8);
    write_reg(13'h224, 32'd64);
    write_reg(13'h120, 32'd0);
    write_reg(13'h120, 32'd1);
    repeat (10) @(negedge logic_clk);
    if (commit_pulses != 2) begin
        $error("second commit produced %0d total pulses", commit_pulses);
        $fatal(1);
    end
    if (cfg_config_epoch !== 16'd8 || max_hold_frames_cfg !== 16'd64) begin
        $error("second commit snapshot mismatch: epoch=%0d hold=%0d",
               cfg_config_epoch, max_hold_frames_cfg);
        $fatal(1);
    end

    $display("PASS: reg_ctrl shadow/commit/status path passed");
    $finish;
end

endmodule
