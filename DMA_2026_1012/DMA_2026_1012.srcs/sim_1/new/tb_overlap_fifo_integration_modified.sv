`timescale 1ns / 1ps

// Minimal integration test for:
//
//     overlap_frame (DDS mode) -> AXI4-Stream Data FIFO -> DMA-side monitor
//
// This test uses the real generated FIFO wrapper from the AXI_DMA block
// design.  Most importantly, the FFT output TREADY is driven only by the
// FIFO S_AXIS_TREADY signal; it is not forced or tied to 1.
//
// The FIFO M_AXIS_TREADY signal is held at 1 to model a configured DMA that
// is continuously able to accept data.  This is a different ready signal
// from the FFT-side ready connection being verified.
//
// Expected result:
//   * FIFO S_AXIS_TREADY becomes 1 after reset.
//   * FFT output handshakes are observed at the FIFO input.
//   * Two 1024-beat frames emerge from the FIFO in the original order.
//   * TDATA, TKEEP and TLAST match between FIFO input and output.
//
// No waveform data is written to a text file.

module tb_overlap_fifo_integration_modified;

localparam integer FRAME_BEATS       = 1024;
localparam integer FRAMES_TO_CHECK   = 2;
localparam integer MAX_STORED_BEATS  = FRAME_BEATS * FRAMES_TO_CHECK;
localparam integer TEST_TIMEOUT_NS   = 600_000; // 600 us

// overlap_frame clocks:
//   r_clk = 312.5 MHz
//   w_clk = 312.5 MHz / 32 = 9.765625 MHz
// FIFO output clock models PS pl_clk0 at approximately 100 MHz.
reg r_clk        = 1'b0;
reg w_clk        = 1'b0;
reg dma_axis_clk = 1'b0;

always #1.600  r_clk        = ~r_clk;
always #51.200 w_clk        = ~w_clk;
always #5.000  dma_axis_clk = ~dma_axis_clk;

// All three resets are active low at these module boundaries.
reg r_rst = 1'b0;
reg w_rst = 1'b0;

reg [3:0]  dma_mode        = 4'd0;
reg        dds_resync      = 1'b0;
reg [15:0] dds_phase_step_h = 16'h004E;
reg [15:0] dds_phase_step_l = 16'hA4A9;

// RFADC ports are unused because this test selects MODE_DDS.
reg  [63:0] m00_axis_tdata  = 64'd0;
reg         m00_axis_tvalid = 1'b0;
wire        m00_axis_tready;
reg  [63:0] m01_axis_tdata  = 64'd0;
reg         m01_axis_tvalid = 1'b0;
wire        m01_axis_tready;
reg  [63:0] m10_axis_tdata  = 64'd0;
reg         m10_axis_tvalid = 1'b0;
wire        m10_axis_tready;
reg  [63:0] m11_axis_tdata  = 64'd0;
reg         m11_axis_tvalid = 1'b0;
wire        m11_axis_tready;

// Direct connection between overlap_frame and the FIFO input.
wire [63:0] fft_axis_tdata;
wire        fft_axis_tvalid;
wire        fft_axis_tlast;
wire        fifo_s_axis_tready;

// FIFO output represents the connection to AXI DMA S_AXIS_S2MM.
wire [63:0] fifo_m_axis_tdata;
wire [7:0]  fifo_m_axis_tkeep;
wire        fifo_m_axis_tvalid;
wire        fifo_m_axis_tlast;
reg         fifo_m_axis_tready = 1'b1;

overlap_frame u_overlap_frame (
    .w_rst              (w_rst),
    .w_clk              (w_clk),
    .r_rst              (r_rst),
    .r_clk              (r_clk),
    .dma_mode           (dma_mode),
    .dds_resync         (dds_resync),
    .dds_phase_step_h   (dds_phase_step_h),
    .dds_phase_step_l   (dds_phase_step_l),
    .m00_axis_tdata     (m00_axis_tdata),
    .m00_axis_tvalid    (m00_axis_tvalid),
    .m00_axis_tready    (m00_axis_tready),
    .m01_axis_tdata     (m01_axis_tdata),
    .m01_axis_tvalid    (m01_axis_tvalid),
    .m01_axis_tready    (m01_axis_tready),
    .m10_axis_tdata     (m10_axis_tdata),
    .m10_axis_tvalid    (m10_axis_tvalid),
    .m10_axis_tready    (m10_axis_tready),
    .m11_axis_tdata     (m11_axis_tdata),
    .m11_axis_tvalid    (m11_axis_tvalid),
    .m11_axis_tready    (m11_axis_tready),
    .m_axis_data_tready (fifo_s_axis_tready),
    .m_axis_data_tdata  (fft_axis_tdata),
    .m_axis_data_tvalid (fft_axis_tvalid),
    .m_axis_data_tlast  (fft_axis_tlast)
);

// Exact generated wrapper of axis_data_fifo_0 in the current AXI_DMA BD.
AXI_DMA_axis_data_fifo_0_0 u_axis_data_fifo (
    .s_axis_aresetn (r_rst),
    .s_axis_aclk    (r_clk),
    .s_axis_tvalid  (fft_axis_tvalid),
    .s_axis_tready  (fifo_s_axis_tready),
    .s_axis_tdata   (fft_axis_tdata),
    .s_axis_tkeep   (8'hFF),
    .s_axis_tlast   (fft_axis_tlast),
    .m_axis_aclk    (dma_axis_clk),
    .m_axis_tvalid  (fifo_m_axis_tvalid),
    .m_axis_tready  (fifo_m_axis_tready),
    .m_axis_tdata   (fifo_m_axis_tdata),
    .m_axis_tkeep   (fifo_m_axis_tkeep),
    .m_axis_tlast   (fifo_m_axis_tlast)
);

// These counters and flags are intentionally visible in the waveform window.
integer fifo_input_beat_count  = 0;
integer fifo_output_beat_count = 0;
integer fifo_input_frame_count = 0;
integer fifo_output_frame_count = 0;
integer input_beat_in_frame    = 0;
integer output_beat_in_frame   = 0;
integer error_count            = 0;

reg saw_fifo_ready       = 1'b0;
reg saw_dds_valid        = 1'b0;
reg saw_fft_valid        = 1'b0;
reg saw_fifo_input       = 1'b0;
reg saw_fifo_output      = 1'b0;
reg saw_nonzero_output   = 1'b0;
reg unexpected_ready_low = 1'b0;
reg test_completed       = 1'b0;

// Scoreboard: capture what the FIFO actually accepts at S_AXIS, then compare
// it with what emerges from M_AXIS.  The array stores the two checked frames.
reg [63:0] expected_data [0:MAX_STORED_BEATS-1];
reg [7:0]  expected_keep [0:MAX_STORED_BEATS-1];
reg        expected_last [0:MAX_STORED_BEATS-1];

// Reset and start DDS mode.
initial begin
    fifo_m_axis_tready = 1'b1;

    fork
        begin
            repeat (20) @(posedge r_clk);
            @(negedge r_clk);
            r_rst = 1'b1;
        end
        begin
            repeat (4) @(posedge w_clk);
            @(negedge w_clk);
            w_rst = 1'b1;
        end
    join

    // Give the asynchronous FIFO reset time to settle in both domains.
    repeat (16) @(posedge dma_axis_clk);
    wait (fifo_s_axis_tready === 1'b1);

    // Select internal DDS.  overlap_frame performs its own mode-change reset.
    @(negedge r_clk);
    dma_mode = 4'd1;

    repeat (48) @(posedge r_clk);
    @(negedge r_clk);
    dds_resync = 1'b1;
    @(negedge r_clk);
    dds_resync = 1'b0;
end

// Observe important internal pipeline milestones for easier diagnosis if the
// timeout fires before any FIFO traffic appears.
always @(posedge r_clk) begin
    if (!r_rst) begin
        saw_fifo_ready       = 1'b0;
        saw_dds_valid        = 1'b0;
        saw_fft_valid        = 1'b0;
        unexpected_ready_low = 1'b0;
    end else begin
        if (fifo_s_axis_tready === 1'b1)
            saw_fifo_ready = 1'b1;
        if (u_overlap_frame.dds_valid === 1'b1)
            saw_dds_valid = 1'b1;
        if (fft_axis_tvalid === 1'b1)
            saw_fft_valid = 1'b1;

        // With the DMA side continuously ready and only two frames checked,
        // this 2048-deep FIFO should not fill.  A low ready is therefore useful
        // diagnostic information, though the AXI handshake remains legal.
        if (fft_axis_tvalid && (fifo_s_axis_tready === 1'b0))
            unexpected_ready_low = 1'b1;
    end
end

// FIFO input-side monitor (312.5 MHz).  A beat enters the FIFO only when both
// TVALID and TREADY are high on this clock edge.
always @(posedge r_clk) begin
    if (!r_rst) begin
        fifo_input_beat_count  = 0;
        fifo_input_frame_count = 0;
        input_beat_in_frame    = 0;
        saw_fifo_input         = 1'b0;
    end else if (fft_axis_tvalid && fifo_s_axis_tready) begin
        saw_fifo_input = 1'b1;

        if (fifo_input_beat_count < MAX_STORED_BEATS) begin
            expected_data[fifo_input_beat_count] = fft_axis_tdata;
            expected_keep[fifo_input_beat_count] = 8'hFF;
            expected_last[fifo_input_beat_count] = fft_axis_tlast;
        end

        if (fft_axis_tlast !== (input_beat_in_frame == FRAME_BEATS - 1)) begin
            $error("[%0t] FIFO input TLAST error at frame %0d beat %0d",
                   $time, fifo_input_frame_count, input_beat_in_frame);
            error_count = error_count + 1;
        end

        fifo_input_beat_count = fifo_input_beat_count + 1;

        if (fft_axis_tlast) begin
            fifo_input_frame_count = fifo_input_frame_count + 1;
            input_beat_in_frame = 0;
            $display("[%0t] FIFO accepted FFT frame %0d (1024 beats)",
                     $time, fifo_input_frame_count);
        end else begin
            input_beat_in_frame = input_beat_in_frame + 1;
        end
    end
end

// FIFO output-side monitor (approximately 100 MHz).  This checks that every
// accepted FFT beat crosses the asynchronous FIFO without corruption.
always @(posedge dma_axis_clk) begin
    if (!r_rst) begin
        fifo_output_beat_count  = 0;
        fifo_output_frame_count = 0;
        output_beat_in_frame    = 0;
        saw_fifo_output         = 1'b0;
        saw_nonzero_output      = 1'b0;
    end else if (fifo_m_axis_tvalid && fifo_m_axis_tready) begin
        saw_fifo_output = 1'b1;
        if (fifo_m_axis_tdata != 64'd0)
            saw_nonzero_output = 1'b1;

        if (fifo_output_beat_count >= fifo_input_beat_count) begin
            $error("[%0t] FIFO produced data before scoreboard capture", $time);
            error_count = error_count + 1;
        end else if (fifo_output_beat_count < MAX_STORED_BEATS) begin
            if (fifo_m_axis_tdata !== expected_data[fifo_output_beat_count]) begin
                $error("[%0t] TDATA mismatch at output beat %0d: expected %h, got %h",
                       $time, fifo_output_beat_count,
                       expected_data[fifo_output_beat_count], fifo_m_axis_tdata);
                error_count = error_count + 1;
            end
            if (fifo_m_axis_tkeep !== expected_keep[fifo_output_beat_count]) begin
                $error("[%0t] TKEEP mismatch at output beat %0d: expected %h, got %h",
                       $time, fifo_output_beat_count,
                       expected_keep[fifo_output_beat_count], fifo_m_axis_tkeep);
                error_count = error_count + 1;
            end
            if (fifo_m_axis_tlast !== expected_last[fifo_output_beat_count]) begin
                $error("[%0t] TLAST mismatch at output beat %0d",
                       $time, fifo_output_beat_count);
                error_count = error_count + 1;
            end
        end

        if (fifo_m_axis_tlast !== (output_beat_in_frame == FRAME_BEATS - 1)) begin
            $error("[%0t] FIFO output frame-length error at frame %0d beat %0d",
                   $time, fifo_output_frame_count, output_beat_in_frame);
            error_count = error_count + 1;
        end

        fifo_output_beat_count = fifo_output_beat_count + 1;

        if (fifo_m_axis_tlast) begin
            fifo_output_frame_count = fifo_output_frame_count + 1;
            output_beat_in_frame = 0;
            $display("[%0t] FIFO delivered frame %0d to the DMA side",
                     $time, fifo_output_frame_count);
        end else begin
            output_beat_in_frame = output_beat_in_frame + 1;
        end
    end
end

// Finish after two complete frames have crossed the real FIFO.
initial begin
    wait (fifo_output_frame_count >= FRAMES_TO_CHECK);
    repeat (8) @(posedge dma_axis_clk);
    test_completed = 1'b1;

    if (!saw_fifo_ready) begin
        $error("FIFO S_AXIS_TREADY never became high");
        error_count = error_count + 1;
    end
    if (!saw_dds_valid) begin
        $error("DDS never generated valid source data");
        error_count = error_count + 1;
    end
    if (!saw_fft_valid || !saw_fifo_input) begin
        $error("No FFT beat was accepted by the FIFO input");
        error_count = error_count + 1;
    end
    if (!saw_fifo_output) begin
        $error("No beat emerged from the FIFO DMA-side interface");
        error_count = error_count + 1;
    end
    if (!saw_nonzero_output) begin
        $error("All observed FIFO output data was zero");
        error_count = error_count + 1;
    end
    if (fifo_input_beat_count < fifo_output_beat_count) begin
        $error("Output beat count exceeded input beat count");
        error_count = error_count + 1;
    end
    if (unexpected_ready_low) begin
        $error("FIFO unexpectedly backpressured the FFT while DMA-side TREADY was 1");
        error_count = error_count + 1;
    end

    $display("------------------------------------------------------------");
    $display("FIFO input beats/frames : %0d / %0d",
             fifo_input_beat_count, fifo_input_frame_count);
    $display("FIFO output beats/frames: %0d / %0d",
             fifo_output_beat_count, fifo_output_frame_count);
    $display("Error count             : %0d", error_count);

    if (error_count == 0)
        $display("PASS: overlap_frame FFT data crossed the real BD FIFO correctly");
    else
        $display("FAIL: integration test detected %0d error(s)", error_count);

    $display("------------------------------------------------------------");
    $finish;
end

// Independent timeout prevents an endless run and reports which stage was
// reached.  In this design the first FFT frame may require more than 100 us.
initial begin
    #(TEST_TIMEOUT_NS);
    if (!test_completed) begin
        $display("------------------------------------------------------------");
        $display("TIMEOUT after %0d ns", TEST_TIMEOUT_NS);
        $display("saw_fifo_ready  = %0d", saw_fifo_ready);
        $display("saw_dds_valid   = %0d", saw_dds_valid);
        $display("saw_fft_valid   = %0d", saw_fft_valid);
        $display("saw_fifo_input  = %0d", saw_fifo_input);
        $display("saw_fifo_output = %0d", saw_fifo_output);
        $display("input beats     = %0d", fifo_input_beat_count);
        $display("output beats    = %0d", fifo_output_beat_count);
        $display("------------------------------------------------------------");
        $fatal(1, "Integration test timed out");
    end
end

endmodule
