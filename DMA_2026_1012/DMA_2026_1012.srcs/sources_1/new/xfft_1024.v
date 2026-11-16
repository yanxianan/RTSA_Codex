`timescale 1ns / 1ps

// 1024-point FFT wrapper with whole-frame admission control.
//
// FIFO requirement:
//   fifo_generator_1 depth must be at least FFT_POINTS.
//   prog_full must assert when fewer than FFT_POINTS free entries remain.
//   Example: depth=2048 and prog_full assert threshold=1024.
module xfft_1024 #(
    parameter integer FFT_POINTS = 1024
)(
    input  wire         rst,
    input  wire         clk,

    input  wire         overlap_wen,
    input  wire [63:0]  overlap_data,

    input  wire         m_axis_data_tready,
    output reg          fft_result_wen,
    output reg  [63:0]  fft_result_data,
    output wire [63:0]  m_axis_data_tdata,
    output wire         m_axis_data_tvalid,
    output wire         m_axis_data_tlast,

    // High only when the complete next input frame can be buffered.
    output wire         frame_can_accept,

    // This should remain low.  If set, the FIFO configuration or admission
    // threshold does not reserve enough room for a complete frame.
    output reg          fifo_overflow_sticky
);

wire        prog_full;
wire        full;
wire        prog_empty;
wire        almost_empty;
wire        empty;
wire        wr_rst_busy;
wire        rd_rst_busy;
wire [63:0] fifo_dout;

wire        s_axis_config_tready;
wire        s_axis_data_tvalid;
wire        s_axis_data_tready;
wire        s_axis_data_tlast;
wire        fifo_rd_en;
wire        fifo_wr_en;

// prog_full is allowed to assert while an admitted frame is being written.
// Therefore it must not gate individual writes; it is used only before a new
// frame starts.  full remains a final defensive guard.
assign frame_can_accept = !rst && !prog_full && !full &&
                          !wr_rst_busy && !rd_rst_busy;
assign fifo_wr_en = overlap_wen && !full && !wr_rst_busy;

always @(posedge clk) begin
    if (rst)
        fifo_overflow_sticky <= 1'b0;
    else if (overlap_wen && (full || wr_rst_busy))
        fifo_overflow_sticky <= 1'b1;
end

fifo_generator_1 fft_fifo_u (
    .clk          (clk),
    .srst         (rst),
    .wr_en        (fifo_wr_en),
    .din          (overlap_data),
    .prog_full    (prog_full),
    .full         (full),
    .rd_en        (fifo_rd_en),
    .dout         (fifo_dout),
    .prog_empty   (prog_empty),
    .almost_empty (almost_empty),
    .empty        (empty),
    .wr_rst_busy  (wr_rst_busy),
    .rd_rst_busy  (rd_rst_busy)
);

reg [15:0] fft_input_count;

assign s_axis_data_tvalid = !empty && !rd_rst_busy;
assign fifo_rd_en         = s_axis_data_tvalid && s_axis_data_tready;
assign s_axis_data_tlast  = s_axis_data_tvalid &&
                            (fft_input_count == (FFT_POINTS - 1));

always @(posedge clk) begin
    if (rst)
        fft_input_count <= 16'd0;
    else if (fifo_rd_en) begin
        if (fft_input_count == (FFT_POINTS - 1))
            fft_input_count <= 16'd0;
        else
            fft_input_count <= fft_input_count + 1'b1;
    end
end

xfft_0 u1_xfft_0 (
    .aclk                     (clk),
    .aresetn                  (!rst),
    .s_axis_config_tdata      ({15'b000000101010101, 1'b1,
                                3'b000, 5'b01010}),
    .s_axis_config_tvalid     (1'b1),
    .s_axis_config_tready     (s_axis_config_tready),
    .s_axis_data_tdata        (fifo_dout),
    .s_axis_data_tvalid       (s_axis_data_tvalid),
    .s_axis_data_tready       (s_axis_data_tready),
    .s_axis_data_tlast        (s_axis_data_tlast),
    .m_axis_data_tdata        (m_axis_data_tdata),
    .m_axis_data_tvalid       (m_axis_data_tvalid),
    .m_axis_data_tready       (m_axis_data_tready),
    .m_axis_data_tlast        (m_axis_data_tlast)
);

wire fft_output_transfer = m_axis_data_tvalid && m_axis_data_tready;

always @(posedge clk) begin
    if (rst) begin
        fft_result_wen  <= 1'b0;
        fft_result_data <= 64'd0;
    end else begin
        fft_result_wen <= fft_output_transfer;
        if (fft_output_transfer)
            fft_result_data <= m_axis_data_tdata;
    end
end

// Internal counters retained for hardware debug.
reg [31:0] fft_in_frame_num;
reg [31:0] fft_out_frame_num;

always @(posedge clk) begin
    if (rst)
        fft_in_frame_num <= 32'd0;
    else if (s_axis_data_tlast && s_axis_data_tvalid && s_axis_data_tready)
        fft_in_frame_num <= fft_in_frame_num + 1'b1;
end

always @(posedge clk) begin
    if (rst)
        fft_out_frame_num <= 32'd0;
    else if (m_axis_data_tlast && m_axis_data_tvalid && m_axis_data_tready)
        fft_out_frame_num <= fft_out_frame_num + 1'b1;
end

endmodule
