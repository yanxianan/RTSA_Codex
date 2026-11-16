`timescale 1ns / 1ps

// Lightweight integration test for the decoupled control plane:
//   AXI BRAM writes -> reg_ctrl staging/snapshot -> pl_config_latch
// No DMA, FFT or RFDC is instantiated, so this runs in milliseconds while
// still covering the exact wiring used by logic_top.
module ila_bram_ctrl_intf (
    input wire clk,
    input wire probe0,
    input wire probe1,
    input wire [12:0] probe2,
    input wire [31:0] probe3
);
endmodule

module tb_config_pipeline;

reg logic_clk = 0;
reg axi_bram_clk = 0;
reg axi_bram_rst = 1;
reg [12:0] axi_bram_addr = 0;
reg [31:0] axi_bram_wd_din = 0;
reg axi_bram_en = 0;
reg [3:0] axi_bram_we = 0;

wire [31:0] axi_bram_rd_dout;
wire [15:0] dds_phase_step_h;
wire [15:0] dds_phase_step_l;
wire [3:0] dma_mode;
wire [15:0] cic_rate_cfg;
wire [7:0] cic_trunc_cfg;
wire [2:0] hb_level;
wire [15:0] max_hold_frames_cfg;
wire [63:0] cfg_center_freq_hz;
wire [63:0] cfg_span_hz;
wire [7:0] cfg_trace_mode;
wire [15:0] cfg_accum_frames;
wire [7:0] cfg_window_type;
wire [31:0] cfg_cal_offset_q16_16;
wire [15:0] cfg_config_epoch;
wire config_commit_pulse;

wire [63:0] active_center_freq_hz;
wire [63:0] active_span_hz;
wire [7:0] active_trace_mode;
wire [15:0] active_accum_frames;
wire [7:0] active_window_type;
wire [31:0] active_cal_offset_q16_16;
wire [15:0] active_config_epoch;
wire [3:0] active_dma_mode;
wire [31:0] active_dds_phase_step;
wire [15:0] active_cic_rate;
wire [7:0] active_cic_trunc;
wire [2:0] active_hb_level;
wire [15:0] active_max_hold_frames;
wire cfg_hold;
wire datapath_reset;
wire cic_cfg_valid;
wire dds_resync;
wire config_busy;

reg frame_idle = 1'b1;
reg frame_complete = 1'b0;
reg [7:0] cic_busy_count = 0;
wire cic_busy = (cic_busy_count != 0);

integer dds_pulses = 0;
integer cic_pulses = 0;
integer reset_cycles = 0;
integer i;

always #5 logic_clk = ~logic_clk;
always #5 axi_bram_clk = ~axi_bram_clk;

always @(posedge logic_clk) begin
    if (axi_bram_rst) begin
        cic_busy_count <= 8'd0;
    end else if (cic_cfg_valid) begin
        cic_busy_count <= 8'd8;
    end else if (cic_busy_count != 0) begin
        cic_busy_count <= cic_busy_count - 1'b1;
    end
end

always @(posedge logic_clk) begin
    if (!axi_bram_rst) begin
        if (dds_resync)
            dds_pulses = dds_pulses + 1;
        if (cic_cfg_valid)
            cic_pulses = cic_pulses + 1;
        if (datapath_reset)
            reset_cycles = reset_cycles + 1;
    end
end

reg_ctrl u_reg_ctrl (
    .logic_clk(logic_clk),
    .axi_bram_addr(axi_bram_addr),
    .axi_bram_clk(axi_bram_clk),
    .axi_bram_wd_din(axi_bram_wd_din),
    .axi_bram_rd_dout(axi_bram_rd_dout),
    .axi_bram_en(axi_bram_en),
    .axi_bram_rst(axi_bram_rst),
    .axi_bram_we(axi_bram_we),
    .cic_cfg_busy(cic_busy),
    .trigger_latched(1'b0),
    .trigger_bin(10'd0),
    .capture_ready(1'b0),
    .capture_busy(1'b0),
    .capture_triggered(1'b0),
    .capture_done(1'b0),
    .config_busy(config_busy),
    .active_config_epoch(active_config_epoch),
    .reg_clk(),
    .reg_addr(),
    .reg_wr_en(),
    .reg_rd_en(),
    .reg_data(),
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

pl_config_latch u_latch (
    .clk(logic_clk),
    .rst(axi_bram_rst),
    .commit_pulse(config_commit_pulse),
    .frame_idle(frame_idle),
    .frame_complete(frame_complete),
    .cic_busy(cic_busy),
    .shadow_center_freq_hz(cfg_center_freq_hz),
    .shadow_span_hz(cfg_span_hz),
    .shadow_trace_mode(cfg_trace_mode),
    .shadow_accum_frames(cfg_accum_frames),
    .shadow_window_type(cfg_window_type),
    .shadow_cal_offset_q16_16(cfg_cal_offset_q16_16),
    .shadow_config_epoch(cfg_config_epoch),
    .shadow_dma_mode(dma_mode),
    .shadow_dds_phase_step({dds_phase_step_h, dds_phase_step_l}),
    .shadow_cic_rate(cic_rate_cfg),
    .shadow_cic_trunc(cic_trunc_cfg),
    .shadow_hb_level(hb_level),
    .shadow_max_hold_frames(max_hold_frames_cfg),
    .active_center_freq_hz(active_center_freq_hz),
    .active_span_hz(active_span_hz),
    .active_trace_mode(active_trace_mode),
    .active_accum_frames(active_accum_frames),
    .active_window_type(active_window_type),
    .active_cal_offset_q16_16(active_cal_offset_q16_16),
    .active_config_epoch(active_config_epoch),
    .active_dma_mode(active_dma_mode),
    .active_dds_phase_step(active_dds_phase_step),
    .active_cic_rate(active_cic_rate),
    .active_cic_trunc(active_cic_trunc),
    .active_hb_level(active_hb_level),
    .active_max_hold_frames(active_max_hold_frames),
    .cfg_hold(cfg_hold),
    .datapath_reset(datapath_reset),
    .cic_cfg_valid(cic_cfg_valid),
    .dds_resync(dds_resync),
    .config_busy(config_busy)
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
        repeat (18) @(negedge axi_bram_clk);
    end
endtask

task commit;
    begin
        write_reg(13'h120, 32'd0);
        write_reg(13'h120, 32'd1);
    end
endtask

task wait_busy_clear;
    begin
        i = 0;
        while (config_busy && i < 1000) begin
            @(negedge logic_clk);
            i = i + 1;
        end
        if (config_busy) begin
            $error("config_busy stuck");
            $fatal(1);
        end
    end
endtask

initial begin
    repeat (10) @(negedge axi_bram_clk);
    axi_bram_rst = 1'b0;
    repeat (10) @(negedge logic_clk);

    // Soft change: only Max Hold, applied at the frame boundary.
    write_reg(13'h224, 32'd2);
    commit();
    wait_busy_clear();
    repeat (4) @(negedge logic_clk);
    if (active_max_hold_frames !== 16'd2 || active_config_epoch !== 16'd1
        || reset_cycles != 0 || cic_pulses != 0 || dds_pulses != 0) begin
        $error("soft commit mismatch: hold=%0d epoch=%0d reset=%0d cic=%0d dds=%0d",
               active_max_hold_frames, active_config_epoch,
               reset_cycles, cic_pulses, dds_pulses);
        $fatal(1);
    end

    // DDS-only change: one resync pulse, no datapath reset.
    write_reg(13'h200, 32'h0100);
    write_reg(13'h204, 32'h0000);
    write_reg(13'h11C, 32'd2);
    commit();
    wait_busy_clear();
    repeat (4) @(negedge logic_clk);
    if (active_dds_phase_step !== 32'h01000000
        || active_config_epoch !== 16'd2
        || dds_pulses != 1 || cic_pulses != 0 || reset_cycles != 0) begin
        $error("DDS commit mismatch: dds=%0d cic=%0d reset=%0d",
               dds_pulses, cic_pulses, reset_cycles);
        $fatal(1);
    end

    // Hard change: CIC/HB -> PL-only datapath reset + one CIC request.
    write_reg(13'h20C, 32'd16);
    write_reg(13'h218, 32'd3);
    write_reg(13'h224, 32'd64);
    write_reg(13'h11C, 32'd3);
    commit();
    wait_busy_clear();
    repeat (4) @(negedge logic_clk);
    if (active_cic_rate !== 16'd16 || active_hb_level !== 3'd3
        || active_max_hold_frames !== 16'd64
        || active_config_epoch !== 16'd3
        || reset_cycles < 32 || cic_pulses != 1 || dds_pulses != 2) begin
        $error("hard commit mismatch: cic=%0d/%0d hb=%0d hold=%0d epoch=%0d reset=%0d pulses=%0d/%0d",
               active_cic_rate, active_cic_trunc, active_hb_level,
               active_max_hold_frames, active_config_epoch,
               reset_cycles, cic_pulses, dds_pulses);
        $fatal(1);
    end

    // CONFIG_BUSY/ACTIVE_EPOCH are readable from the AXI register map.
    @(negedge axi_bram_clk);
    axi_bram_addr = 13'h134;
    axi_bram_en = 1'b1;
    axi_bram_we = 4'd0;
    repeat (8) @(negedge axi_bram_clk);
    if (axi_bram_rd_dout[15:0] !== 16'd3) begin
        $error("ACTIVE_EPOCH readback mismatch: %h", axi_bram_rd_dout);
        $fatal(1);
    end
    axi_bram_en = 1'b0;

    $display("PASS: reg_ctrl -> commit snapshot -> frame-boundary pipeline passed");
    $finish;
end

initial begin
    #100000;
    $fatal(1, "timeout");
end

endmodule
