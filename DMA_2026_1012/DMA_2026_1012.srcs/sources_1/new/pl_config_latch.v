`timescale 1ns / 1ps

// Frame-synchronized PL configuration sequencer.
//
// PS writes staging registers and toggles commit. The register block takes an
// atomic snapshot of all staging registers on commit, then this module applies
// that committed snapshot only after the current formatted spectrum frame has
// been completely transmitted (or when the frame formatter is idle).
//
// Configuration classes:
//   soft : header fields and Max Hold. Frame-boundary latch only.
//   dds  : DDS phase step. Frame-boundary latch + one DDS resync pulse.
//   hard : source, CIC rate/truncation, HB level. Frame-boundary latch +
//          PL datapath reset + CIC reconfiguration + DDS resync.
//
// The AXI DMA, its descriptor ring and /dev/rtsa_dma are never touched by this
// sequencer. The formatter simply pauses between frames while the PL datapath
// is reconfigured, so the DMA stream stays valid without a stop/restart.
module pl_config_latch (
    input  wire         clk,
    input  wire         rst,

    input  wire         commit_pulse,
    input  wire         frame_idle,
    input  wire         frame_complete,
    input  wire         cic_busy,

    input  wire [63:0]  shadow_center_freq_hz,
    input  wire [63:0]  shadow_span_hz,
    input  wire [7:0]   shadow_trace_mode,
    input  wire [15:0]  shadow_accum_frames,
    input  wire [7:0]   shadow_window_type,
    input  wire [31:0]  shadow_cal_offset_q16_16,
    input  wire [15:0]  shadow_config_epoch,
    input  wire [3:0]   shadow_dma_mode,
    input  wire [31:0]  shadow_dds_phase_step,
    input  wire [15:0]  shadow_cic_rate,
    input  wire [7:0]   shadow_cic_trunc,
    input  wire [2:0]   shadow_hb_level,
    input  wire [15:0]  shadow_max_hold_frames,

    output reg  [63:0]  active_center_freq_hz,
    output reg  [63:0]  active_span_hz,
    output reg  [7:0]   active_trace_mode,
    output reg  [15:0]  active_accum_frames,
    output reg  [7:0]   active_window_type,
    output reg  [31:0]  active_cal_offset_q16_16,
    output reg  [15:0]  active_config_epoch,
    output reg  [3:0]   active_dma_mode,
    output reg  [31:0]  active_dds_phase_step,
    output reg  [15:0]  active_cic_rate,
    output reg  [7:0]   active_cic_trunc,
    output reg  [2:0]   active_hb_level,
    output reg  [15:0]  active_max_hold_frames,

    output reg          cfg_hold,
    output reg          datapath_reset,
    output reg          cic_cfg_valid,
    output reg          dds_resync,
    output reg          config_busy
);

localparam [2:0] S_IDLE      = 3'd0;
localparam [2:0] S_WAIT      = 3'd1;
localparam [2:0] S_RESET     = 3'd2;
localparam [2:0] S_CIC_WAIT  = 3'd3;
localparam [2:0] S_CIC_PULSE = 3'd4;
localparam [2:0] S_CIC_DONE  = 3'd5;
localparam [2:0] S_RELEASE   = 3'd6;
localparam [2:0] S_DDS_PULSE = 3'd7;

localparam [5:0] RESET_CYCLES = 6'd31;
localparam [11:0] CIC_MIN_WAIT = 12'd63;
localparam [11:0] CIC_TIMEOUT  = 12'd2047;

reg [2:0] state;
reg [5:0] reset_count;
reg [11:0] wait_count;
reg        pending;
reg        cic_seen_busy;

wire frame_boundary = frame_idle || frame_complete;
wire source_change =
    (shadow_dma_mode != active_dma_mode) ||
    (shadow_hb_level != active_hb_level);
wire filter_change =
    (shadow_cic_rate != active_cic_rate) ||
    (shadow_cic_trunc != active_cic_trunc);
wire hard_change = source_change || filter_change;
wire dds_change  = (shadow_dds_phase_step != active_dds_phase_step);

always @(posedge clk) begin
    if (rst) begin
        state                     <= S_IDLE;
        reset_count               <= 6'd0;
        wait_count                <= 12'd0;
        pending                   <= 1'b0;
        cic_seen_busy             <= 1'b0;
        cfg_hold                  <= 1'b0;
        datapath_reset            <= 1'b0;
        cic_cfg_valid             <= 1'b0;
        dds_resync                <= 1'b0;
        config_busy               <= 1'b0;
        active_center_freq_hz     <= 64'd0;
        active_span_hz            <= 64'd9765625;
        active_trace_mode         <= 8'd1;
        active_accum_frames       <= 16'd80;
        active_window_type        <= 8'd4;
        active_cal_offset_q16_16  <= -32'sd12451840;
        active_config_epoch       <= 16'd1;
        active_dma_mode           <= 4'd1;
        active_dds_phase_step     <= 32'h004EA4A9;
        active_cic_rate           <= 16'd8;
        active_cic_trunc          <= 8'd31;
        active_hb_level           <= 3'd2;
        active_max_hold_frames    <= 16'd80;
    end else begin
        if (commit_pulse && state != S_IDLE) begin
            pending <= 1'b1;
        end

        case (state)
            S_IDLE: begin
                cfg_hold        <= 1'b0;
                datapath_reset  <= 1'b0;
                cic_cfg_valid   <= 1'b0;
                dds_resync      <= 1'b0;
                config_busy     <= 1'b0;
                if (commit_pulse) begin
                    cfg_hold     <= 1'b1;
                    config_busy  <= 1'b1;
                    pending      <= 1'b0;
                    state        <= S_WAIT;
                end
            end

            S_WAIT: begin
                cfg_hold    <= 1'b1;
                config_busy <= 1'b1;
                if (frame_boundary) begin
                    active_center_freq_hz    <= shadow_center_freq_hz;
                    active_span_hz           <= shadow_span_hz;
                    active_trace_mode        <= shadow_trace_mode;
                    active_accum_frames      <= shadow_accum_frames;
                    active_window_type       <= shadow_window_type;
                    active_cal_offset_q16_16 <= shadow_cal_offset_q16_16;
                    active_config_epoch      <= shadow_config_epoch;
                    active_dma_mode          <= shadow_dma_mode;
                    active_dds_phase_step    <= shadow_dds_phase_step;
                    active_cic_rate          <= shadow_cic_rate;
                    active_cic_trunc         <= shadow_cic_trunc;
                    active_hb_level          <= shadow_hb_level;
                    active_max_hold_frames   <= shadow_max_hold_frames;

                    // The latest shadow values have been captured. A commit
                    // that arrives later will set pending again.
                    pending <= 1'b0;
                    if (hard_change) begin
                        datapath_reset <= 1'b1;
                        reset_count    <= RESET_CYCLES;
                        state          <= S_RESET;
                    end else if (dds_change) begin
                        dds_resync <= 1'b1;
                        state      <= S_DDS_PULSE;
                    end else begin
                        cfg_hold    <= 1'b0;
                        config_busy <= 1'b0;
                        state       <= S_IDLE;
                    end
                end
            end

            S_RESET: begin
                cfg_hold       <= 1'b1;
                datapath_reset <= 1'b1;
                config_busy    <= 1'b1;
                if (reset_count == 6'd0) begin
                    datapath_reset <= 1'b0;
                    wait_count     <= 12'd0;
                    cic_seen_busy  <= 1'b0;
                    state          <= S_CIC_WAIT;
                end else begin
                    reset_count <= reset_count - 1'b1;
                end
            end

            S_CIC_WAIT: begin
                cfg_hold    <= 1'b1;
                config_busy <= 1'b1;
                if (wait_count < CIC_MIN_WAIT) begin
                    wait_count <= wait_count + 1'b1;
                end else if (!cic_busy || wait_count >= CIC_TIMEOUT) begin
                    cic_seen_busy <= 1'b0;
                    wait_count    <= 12'd0;
                    state         <= S_CIC_PULSE;
                end else begin
                    wait_count <= wait_count + 1'b1;
                end
            end

            S_CIC_PULSE: begin
                cfg_hold       <= 1'b1;
                config_busy    <= 1'b1;
                cic_cfg_valid  <= 1'b1;
                wait_count     <= 12'd0;
                state          <= S_CIC_DONE;
            end

            S_CIC_DONE: begin
                cfg_hold       <= 1'b1;
                config_busy    <= 1'b1;
                cic_cfg_valid  <= 1'b0;
                if (cic_busy) begin
                    cic_seen_busy <= 1'b1;
                end
                if ((cic_seen_busy && !cic_busy) || wait_count >= CIC_TIMEOUT) begin
                    dds_resync <= 1'b1;
                    wait_count <= 12'd0;
                    state      <= S_RELEASE;
                end else begin
                    wait_count <= wait_count + 1'b1;
                end
            end

            S_DDS_PULSE: begin
                cfg_hold    <= 1'b1;
                config_busy <= 1'b1;
                dds_resync  <= 1'b0;
                state       <= S_RELEASE;
            end

            S_RELEASE: begin
                cic_cfg_valid  <= 1'b0;
                dds_resync     <= 1'b0;
                datapath_reset <= 1'b0;
                if (pending) begin
                    pending     <= 1'b0;
                    cfg_hold    <= 1'b1;
                    config_busy <= 1'b1;
                    state       <= S_WAIT;
                end else begin
                    cfg_hold    <= 1'b0;
                    config_busy <= 1'b0;
                    state       <= S_IDLE;
                end
            end

            default: begin
                cfg_hold       <= 1'b0;
                datapath_reset <= 1'b0;
                cic_cfg_valid  <= 1'b0;
                dds_resync     <= 1'b0;
                config_busy    <= 1'b0;
                pending        <= 1'b0;
                state          <= S_IDLE;
            end
        endcase
    end
end

endmodule
