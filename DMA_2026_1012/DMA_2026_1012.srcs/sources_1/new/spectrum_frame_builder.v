`timescale 1ns / 1ps

// Converts the complex FFT max-hold stream into the PL-PS spectrum frame:
//   64-byte versioned header + BIN_COUNT x int16 log-magnitude bins.
// Two int16 bins are packed into each 64-bit AXI4-Stream beat.
//
// V1 constants:
//   bin_count = 1024, frame_bytes = 2112, trace_mode = Max Hold,
//   accumulation_frames = 80, Blackman-Harris, 0.01 dB/LSB, dBFS.
module spectrum_frame_builder #(
    parameter integer BIN_COUNT = 1024
)(
    input  wire        clk,
    input  wire        rst,

    // Header configuration, stable before a frame begins.
    input  wire [63:0] cfg_center_freq_hz,
    input  wire [63:0] cfg_span_hz,
    input  wire [7:0]  cfg_trace_mode,
    input  wire [15:0] cfg_accum_frames,
    input  wire [7:0]  cfg_window_type,
    input  wire [31:0] cfg_cal_offset_q16_16,
    input  wire [15:0] cfg_config_epoch,
    input  wire        cfg_hold,

    // Complex FFT bins: [31:0] real, [63:32] imag
    input  wire [63:0] s_axis_tdata,
    input  wire        s_axis_tvalid,
    output wire        s_axis_tready,
    input  wire        s_axis_tlast,

    // Formatted spectrum frame
    output wire [63:0] m_axis_tdata,
    output wire [7:0]  m_axis_tkeep,
    output wire        m_axis_tvalid,
    input  wire        m_axis_tready,
    output wire        m_axis_tlast,
    output wire        frame_idle,
    output wire        frame_complete
);

localparam integer HEADER_BYTES  = 64;
localparam integer FRAME_BYTES   = HEADER_BYTES + BIN_COUNT * 2;
localparam integer BEAT_COUNT    = BIN_COUNT / 4;

localparam [31:0] MAGIC          = 32'h52545341;
localparam [15:0] VERSION        = 16'd1;
localparam [15:0] HEADER_WORDS   = 16'd64;
localparam [31:0] FLAGS          = 32'h00000101; // LOG_FORMAT | FRAME_VALID
localparam [7:0]  DATA_FORMAT    = 8'd0;         // int16 log 0.01 dB
localparam [7:0]  UNIT           = 8'd0;         // dBFS

localparam [1:0] ST_IDLE    = 2'd0;
localparam [1:0] ST_HEADER  = 2'd1;
localparam [1:0] ST_PAYLOAD = 2'd2;
localparam [1:0] ST_DONE    = 2'd3;

reg [1:0]  state;
reg [3:0]  header_index;
reg [16:0] input_count;
reg        input_done;
reg [15:0] beat_count;

reg [63:0] sequence;
reg [63:0] sequence_latched;
reg [63:0] timestamp_counter;
reg [63:0] timestamp_latched;

reg [63:0] center_latched;
reg [63:0] span_latched;
reg [7:0]  trace_latched;
reg [15:0] accum_latched;
reg [7:0]  window_latched;
reg [31:0] cal_latched;
reg [15:0] epoch_latched;

reg [15:0] fifo_mem [0:15];
reg [4:0]  fifo_wr_ptr;
reg [4:0]  fifo_rd_ptr;
reg [4:0]  fifo_count;

wire signed [15:0] log_value;
wire               log_valid;
wire               log_ready = (fifo_count < 5'd8);

wire fifo_write = log_valid && (fifo_count < 5'd16);
wire [3:0] rd0 = fifo_rd_ptr[3:0];
wire [3:0] rd1 = rd0 + 4'd1;
wire [3:0] rd2 = rd0 + 4'd2;
wire [3:0] rd3 = rd0 + 4'd3;

wire payload_phase = (state == ST_PAYLOAD) && (fifo_count >= 5'd4);
wire header_phase  = (state == ST_HEADER);
wire payload_last  = (beat_count == BEAT_COUNT - 1);
wire payload_transfer = payload_phase && m_axis_tready;

assign s_axis_tready = (state == ST_PAYLOAD) && !input_done && log_ready;
assign m_axis_tvalid = header_phase || payload_phase;
assign m_axis_tkeep  = 8'hFF;
assign m_axis_tlast  = payload_phase && payload_last;
assign frame_idle    = (state == ST_IDLE);
assign frame_complete = (state == ST_DONE);

function [63:0] header_word;
    input [3:0] index;
    begin
        case (index)
            4'd0: header_word = {HEADER_WORDS, VERSION, MAGIC};
            4'd1: header_word = {FLAGS, FRAME_BYTES};
            4'd2: header_word = sequence_latched;
            4'd3: header_word = timestamp_latched;
            4'd4: header_word = center_latched;
            4'd5: header_word = span_latched;
            4'd6: header_word = {window_latched, accum_latched, trace_latched,
                                 BIN_COUNT};
            4'd7: header_word = {epoch_latched, UNIT, DATA_FORMAT,
                                 cal_latched};
            default: header_word = 64'd0;
        endcase
    end
endfunction

assign m_axis_tdata = header_phase
    ? header_word(header_index)
    : {fifo_mem[rd3], fifo_mem[rd2], fifo_mem[rd1], fifo_mem[rd0]};

// ---------------------------------------------------------------------------
// Log magnitude pipeline
// ---------------------------------------------------------------------------
log_magnitude u_log_magnitude (
    .clk       (clk),
    .rst       (rst),
    .s_real    (s_axis_tdata[31:0]),
    .s_imag    (s_axis_tdata[63:32]),
    .s_valid   (s_axis_tvalid && s_axis_tready),
    .m_value   (log_value),
    .m_valid   (log_valid)
);

// ---------------------------------------------------------------------------
// Control
// ---------------------------------------------------------------------------
always @(posedge clk) begin
    if (rst) begin
        state              <= ST_IDLE;
        header_index       <= 4'd0;
        input_count        <= 17'd0;
        input_done         <= 1'b0;
        beat_count         <= 16'd0;
        sequence           <= 64'd0;
        sequence_latched   <= 64'd0;
        timestamp_counter  <= 64'd0;
        timestamp_latched  <= 64'd0;
        center_latched     <= 64'd0;
        span_latched       <= 64'd9765625;
        trace_latched      <= 8'd1;
        accum_latched      <= 16'd80;
        window_latched     <= 8'd4;
        cal_latched        <= -32'sd12451840;
        epoch_latched      <= 16'd1;
        fifo_wr_ptr        <= 5'd0;
        fifo_rd_ptr        <= 5'd0;
        fifo_count         <= 5'd0;
    end else begin
        timestamp_counter <= timestamp_counter + 1'b1;

        case (state)
            ST_IDLE: begin
                if (s_axis_tvalid && !cfg_hold) begin
                    sequence_latched  <= sequence;
                    timestamp_latched <= timestamp_counter;
                    center_latched    <= cfg_center_freq_hz;
                    span_latched      <= cfg_span_hz;
                    trace_latched     <= cfg_trace_mode;
                    accum_latched     <= cfg_accum_frames;
                    window_latched    <= cfg_window_type;
                    cal_latched       <= cfg_cal_offset_q16_16;
                    epoch_latched     <= cfg_config_epoch;
                    header_index      <= 4'd0;
                    input_count       <= 17'd0;
                    input_done        <= 1'b0;
                    beat_count        <= 16'd0;
                    fifo_wr_ptr       <= 5'd0;
                    fifo_rd_ptr       <= 5'd0;
                    fifo_count        <= 5'd0;
                    state             <= ST_HEADER;
                end
            end

            ST_HEADER: begin
                if (m_axis_tready) begin
                    if (header_index == 4'd7) begin
                        header_index <= 4'd0;
                        state        <= ST_PAYLOAD;
                    end else begin
                        header_index <= header_index + 1'b1;
                    end
                end
            end

            ST_PAYLOAD: begin
                if (s_axis_tvalid && s_axis_tready) begin
                    if (input_count == BIN_COUNT - 1) begin
                        input_done <= 1'b1;
                    end
                    input_count <= input_count + 1'b1;
                end

                if (payload_transfer) begin
                    fifo_rd_ptr <= fifo_rd_ptr + 5'd4;
                    if (payload_last) begin
                        state <= ST_DONE;
                    end else begin
                        beat_count <= beat_count + 1'b1;
                    end
                end
            end

            ST_DONE: begin
                sequence <= sequence + 1'b1;
                state    <= ST_IDLE;
            end

            default: state <= ST_IDLE;
        endcase

        // FIFO write and count update. A write and a read can coincide.
        if (fifo_write) begin
            fifo_mem[fifo_wr_ptr[3:0]] <= log_value;
            fifo_wr_ptr <= fifo_wr_ptr + 1'b1;
        end

        if (fifo_write && !payload_transfer) begin
            fifo_count <= fifo_count + 1'b1;
        end else if (!fifo_write && payload_transfer) begin
            fifo_count <= fifo_count - 5'd4;
        end else if (fifo_write && payload_transfer) begin
            fifo_count <= fifo_count - 5'd3;
        end
    end
end

endmodule
