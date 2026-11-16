`timescale 1ns / 1ps

// Standalone test for the frame-boundary configuration sequencer.
// It verifies that:
//   - a commit waits for the frame boundary before changing active config;
//   - soft changes never reset the datapath or pulse CIC/DDS;
//   - DDS-only changes emit exactly one resync pulse;
//   - hard changes reset the datapath, reconfigure CIC and resync DDS;
//   - CONFIG_BUSY stays asserted for the whole transaction;
//   - a newer commit arriving while busy wins at the next boundary.
module tb_config_latch;

reg clk = 1'b0;
reg rst = 1'b1;
reg commit_pulse = 1'b0;
reg frame_idle = 1'b1;
reg frame_complete = 1'b0;

reg [63:0] shadow_center_freq_hz = 64'd0;
reg [63:0] shadow_span_hz = 64'd9765625;
reg [7:0]  shadow_trace_mode = 8'd1;
reg [15:0] shadow_accum_frames = 16'd80;
reg [7:0]  shadow_window_type = 8'd4;
reg [31:0] shadow_cal_offset_q16_16 = -32'sd12451840;
reg [15:0] shadow_config_epoch = 16'd1;
reg [3:0]  shadow_dma_mode = 4'd1;
reg [31:0] shadow_dds_phase_step = 32'h004EA4A9;
reg [15:0] shadow_cic_rate = 16'd8;
reg [7:0]  shadow_cic_trunc = 8'd31;
reg [2:0]  shadow_hb_level = 3'd2;
reg [15:0] shadow_max_hold_frames = 16'd80;

wire [63:0] active_center_freq_hz;
wire [63:0] active_span_hz;
wire [7:0]  active_trace_mode;
wire [15:0] active_accum_frames;
wire [7:0]  active_window_type;
wire [31:0] active_cal_offset_q16_16;
wire [15:0] active_config_epoch;
wire [3:0]  active_dma_mode;
wire [31:0] active_dds_phase_step;
wire [15:0] active_cic_rate;
wire [7:0]  active_cic_trunc;
wire [2:0]  active_hb_level;
wire [15:0] active_max_hold_frames;
wire cfg_hold;
wire datapath_reset;
wire cic_cfg_valid;
wire dds_resync;
wire config_busy;

reg [7:0] cic_busy_count = 8'd0;
wire cic_busy = (cic_busy_count != 8'd0);

integer dds_pulses = 0;
integer cic_pulses = 0;
integer reset_cycles = 0;
integer errors = 0;
integer i;

always #5 clk = ~clk;

always @(posedge clk) begin
    if (rst) begin
        cic_busy_count <= 8'd0;
    end else if (cic_cfg_valid) begin
        // Emulate the CIC handshake: busy rises after the request and clears
        // a few clocks later.
        cic_busy_count <= 8'd8;
    end else if (cic_busy_count != 8'd0) begin
        cic_busy_count <= cic_busy_count - 1'b1;
    end
end

always @(posedge clk) begin
    if (!rst) begin
        if (dds_resync)
            dds_pulses = dds_pulses + 1;
        if (cic_cfg_valid)
            cic_pulses = cic_pulses + 1;
        if (datapath_reset)
            reset_cycles = reset_cycles + 1;
        if (config_busy && cfg_hold === 1'bx) begin
            $error("cfg_hold became X while busy");
            errors = errors + 1;
        end
    end
end

pl_config_latch dut (
    .clk                        (clk),
    .rst                        (rst),
    .commit_pulse               (commit_pulse),
    .frame_idle                 (frame_idle),
    .frame_complete             (frame_complete),
    .cic_busy                   (cic_busy),
    .shadow_center_freq_hz      (shadow_center_freq_hz),
    .shadow_span_hz             (shadow_span_hz),
    .shadow_trace_mode          (shadow_trace_mode),
    .shadow_accum_frames        (shadow_accum_frames),
    .shadow_window_type         (shadow_window_type),
    .shadow_cal_offset_q16_16   (shadow_cal_offset_q16_16),
    .shadow_config_epoch        (shadow_config_epoch),
    .shadow_dma_mode            (shadow_dma_mode),
    .shadow_dds_phase_step      (shadow_dds_phase_step),
    .shadow_cic_rate            (shadow_cic_rate),
    .shadow_cic_trunc           (shadow_cic_trunc),
    .shadow_hb_level            (shadow_hb_level),
    .shadow_max_hold_frames     (shadow_max_hold_frames),
    .active_center_freq_hz      (active_center_freq_hz),
    .active_span_hz             (active_span_hz),
    .active_trace_mode          (active_trace_mode),
    .active_accum_frames        (active_accum_frames),
    .active_window_type         (active_window_type),
    .active_cal_offset_q16_16   (active_cal_offset_q16_16),
    .active_config_epoch        (active_config_epoch),
    .active_dma_mode            (active_dma_mode),
    .active_dds_phase_step      (active_dds_phase_step),
    .active_cic_rate            (active_cic_rate),
    .active_cic_trunc           (active_cic_trunc),
    .active_hb_level            (active_hb_level),
    .active_max_hold_frames     (active_max_hold_frames),
    .cfg_hold                   (cfg_hold),
    .datapath_reset             (datapath_reset),
    .cic_cfg_valid              (cic_cfg_valid),
    .dds_resync                 (dds_resync),
    .config_busy                (config_busy)
);

task do_commit;
    begin
        @(negedge clk);
        commit_pulse = 1'b1;
        @(negedge clk);
        commit_pulse = 1'b0;
    end
endtask

task pulse_frame_complete;
    begin
        @(negedge clk);
        frame_complete = 1'b1;
        @(negedge clk);
        frame_complete = 1'b0;
    end
endtask

task fail_if_busy_too_long;
    begin
        i = 0;
        while (config_busy && i < 1000) begin
            @(negedge clk);
            i = i + 1;
        end
        if (config_busy) begin
            $error("config_busy did not clear");
            $fatal(1);
        end
    end
endtask

initial begin
    rst = 1'b1;
    repeat (5) @(negedge clk);
    rst = 1'b0;
    repeat (5) @(negedge clk);

    if (active_dma_mode !== 4'd1 || active_config_epoch !== 16'd1
        || active_cic_rate !== 16'd8 || active_hb_level !== 3'd2
        || active_max_hold_frames !== 16'd80 || cfg_hold !== 1'b0
        || config_busy !== 1'b0) begin
        $error("power-on active defaults mismatch");
        $fatal(1);
    end

    // 1) Soft change must wait for the current frame to finish.
    frame_idle = 1'b0;
    shadow_center_freq_hz = 64'd2400000000;
    shadow_accum_frames = 16'd32;
    shadow_max_hold_frames = 16'd32;
    shadow_config_epoch = 16'd5;
    do_commit();
    repeat (3) @(negedge clk);
    if (cfg_hold !== 1'b1 || config_busy !== 1'b1
        || active_center_freq_hz !== 64'd0
        || active_config_epoch !== 16'd1) begin
        $error("soft change applied before frame boundary");
        $fatal(1);
    end
    if (datapath_reset || cic_pulses != 0 || dds_pulses != 0) begin
        $error("soft change touched the datapath");
        $fatal(1);
    end
    pulse_frame_complete();
    repeat (4) @(negedge clk);
    if (active_center_freq_hz !== 64'd2400000000
        || active_config_epoch !== 16'd5
        || active_accum_frames !== 16'd32
        || active_max_hold_frames !== 16'd32
        || cfg_hold !== 1'b0 || config_busy !== 1'b0) begin
        $error("soft change not applied at frame boundary");
        $fatal(1);
    end
    if (datapath_reset || cic_pulses != 0 || dds_pulses != 0) begin
        $error("soft change unexpectedly pulsed datapath controls");
        $fatal(1);
    end
    frame_idle = 1'b1;

    // 2) DDS-only change: frame boundary + exactly one resync pulse.
    shadow_dds_phase_step = 32'h01000000;
    shadow_config_epoch = 16'd6;
    do_commit();
    fail_if_busy_too_long();
    repeat (2) @(negedge clk);
    if (active_dds_phase_step !== 32'h01000000
        || active_config_epoch !== 16'd6
        || dds_pulses != 1 || cic_pulses != 0 || reset_cycles != 0) begin
        $error("DDS-only change mismatch: dds=%0d cic=%0d reset=%0d",
               dds_pulses, cic_pulses, reset_cycles);
        $fatal(1);
    end

    // 3) Hard change: reset PL datapath, single CIC request, DDS resync.
    shadow_cic_rate = 16'd16;
    shadow_hb_level = 3'd3;
    shadow_config_epoch = 16'd7;
    do_commit();
    fail_if_busy_too_long();
    repeat (2) @(negedge clk);
    if (active_cic_rate !== 16'd16 || active_hb_level !== 3'd3
        || active_config_epoch !== 16'd7
        || reset_cycles < 32 || cic_pulses != 1 || dds_pulses != 2
        || datapath_reset !== 1'b0 || cfg_hold !== 1'b0) begin
        $error("hard change mismatch: reset=%0d cic=%0d dds=%0d",
               reset_cycles, cic_pulses, dds_pulses);
        $fatal(1);
    end

    // 4) A newer commit while the sequencer is waiting wins at the boundary.
    frame_idle = 1'b0;
    shadow_center_freq_hz = 64'd1000000000;
    shadow_config_epoch = 16'd8;
    do_commit();
    repeat (2) @(negedge clk);
    shadow_center_freq_hz = 64'd3000000000;
    shadow_config_epoch = 16'd9;
    do_commit();
    repeat (2) @(negedge clk);
    pulse_frame_complete();
    fail_if_busy_too_long();
    repeat (2) @(negedge clk);
    if (active_center_freq_hz !== 64'd3000000000
        || active_config_epoch !== 16'd9) begin
        $error("latest pending commit did not win: center=%0d epoch=%0d",
               active_center_freq_hz, active_config_epoch);
        $fatal(1);
    end

    if (errors == 0) begin
        $display("PASS: pl_config_latch frame-boundary sequencing passed");
    end else begin
        $fatal(1, "FAIL: %0d error(s)", errors);
    end
    $finish;
end

initial begin
    #100000;
    $fatal(1, "timeout");
end

endmodule
