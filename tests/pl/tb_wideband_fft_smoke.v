`timescale 1ns / 1ps

// Throughput smoke test for the 312.5 MSPS wideband path:
// continuous one-sample-per-clock input, 0% overlap, max-hold=1.
// It verifies that the frame controller/FFT input FIFO do not overflow and
// that at least one complete spectrum frame is produced.
module tb_wideband_fft_smoke;

reg clk = 1'b0;
reg rst_n = 1'b0;
reg source_enabled = 1'b0;
reg [63:0] data_in = {32'sd0, 32'sd1000};
reg data_in_valid = 1'b0;
reg m_axis_tready = 1'b1;

wire [63:0] m_axis_tdata;
wire        m_axis_tvalid;
wire        m_axis_tlast;

wire [63:0] raw_tdata;
wire        raw_tvalid;
wire        raw_tready;
wire        raw_tlast;

wire        frame_drop_pulse;
wire [31:0] frame_drop_count;
wire        fft_fifo_overflow_sticky;

integer output_beats = 0;
integer output_frames = 0;
integer i;

always #1.6 clk = ~clk;

always @(posedge clk) begin
    if (rst_n && m_axis_tvalid && m_axis_tready) begin
        output_beats = output_beats + 1;
        if (m_axis_tlast)
            output_frames = output_frames + 1;
    end
end

overlap_fft_module dut (
    .clk                      (clk),
    .rst_n                    (rst_n),
    .source_enabled           (source_enabled),
    .data_in                  (data_in),
    .data_in_valid            (data_in_valid),
    .max_hold_frames_cfg      (16'd1),
    .overlap_ratio_cfg        (16'd0),
    .m_axis_data_tready       (m_axis_tready),
    .m_axis_data_tdata        (m_axis_tdata),
    .m_axis_data_tvalid       (m_axis_tvalid),
    .m_axis_data_tlast        (m_axis_tlast),
    .fft_raw_tdata            (raw_tdata),
    .fft_raw_tvalid           (raw_tvalid),
    .fft_raw_tready           (raw_tready),
    .fft_raw_tlast            (raw_tlast),
    .frame_drop_pulse         (frame_drop_pulse),
    .frame_drop_count         (frame_drop_count),
    .fft_fifo_overflow_sticky (fft_fifo_overflow_sticky)
);

initial begin
    repeat (20) @(negedge clk);
    rst_n = 1'b1;
    source_enabled = 1'b1;
    data_in_valid = 1'b1;
    repeat (8000) @(negedge clk);

    if (fft_fifo_overflow_sticky) begin
        $error("FFT input FIFO overflowed");
        $fatal(1);
    end
    if (output_frames < 1) begin
        $error("no complete spectrum frame produced: beats=%0d", output_beats);
        $fatal(1);
    end
    $display("PASS: wideband FFT throughput, beats=%0d frames=%0d drops=%0d",
             output_beats, output_frames, frame_drop_count);
    $finish;
end

initial begin
    #200000;
    $fatal(1, "timeout");
end

endmodule
