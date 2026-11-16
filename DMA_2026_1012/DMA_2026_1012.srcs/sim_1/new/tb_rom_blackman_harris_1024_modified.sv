`timescale 1ns / 1ps

// ============================================================================
// Unit test for rom_blackman_harris_1024
//
// Purpose
//   1. Verify that all 1024 ROM locations contain the expected Q1.17
//      four-term Blackman-Harris coefficients.
//   2. Demonstrate the configured two-stage ROM read latency without relying
//      on simulator delta cycles or on the Xilinx model's 100 ps output delay.
//   3. Establish the interface contract needed by the future window module:
//        request address n -> response coefficient w[n].
//
// Latency convention used by the Block Memory Generator summary
//
//   Rising edge C0:
//     - ena/addra are accepted by the ROM.
//     - the request tag enters latency stage 1.
//
//   Rising edge C1:
//     - the request tag enters latency stage 2.
//     - douta changes to the requested coefficient after the ROM clock-to-out
//       delay (100 ps in the Xilinx behavioral model).
//
//   Falling edge after C1:
//     - douta is fully stable.
//     - response_addr_s2 and douta are compared.
//
// Thus "total read latency = 2" means two ROM pipeline stages. Stage 1 is
// the edge at which the request is accepted; stage 2 is the edge at which the
// registered response appears. The falling-edge checker is only a testbench
// sampling technique; the real design still uses rising-edge logic.
// ============================================================================

module tb_rom_blackman_harris_1024_modified;

    localparam integer ROM_DEPTH           = 1024;
    localparam integer ROM_ADDR_WIDTH      = 10;
    localparam integer ROM_DATA_WIDTH      = 18;
    localparam integer READ_LATENCY_STAGES = 2;
    localparam real    CLK_PERIOD_NS       = 3.2; // 312.5 MHz

    reg                       clka;
    reg                       ena;
    reg  [ROM_ADDR_WIDTH-1:0] addra;
    wire [ROM_DATA_WIDTH-1:0] douta;

    // Transaction tags used only by the checker. They model the two latency
    // stages shown in the IP summary and do not drive the ROM.
    reg                       request_valid_s1;
    reg  [ROM_ADDR_WIDTH-1:0] request_addr_s1;
    reg                       response_valid_s2;
    reg  [ROM_ADDR_WIDTH-1:0] response_addr_s2;

    reg [ROM_DATA_WIDTH-1:0] captured_data  [0:ROM_DEPTH-1];
    reg                      captured_valid [0:ROM_DEPTH-1];

    integer drive_index;
    integer init_index;
    integer capture_count;
    integer error_count;
    integer rising_edge_count;

    reg [ROM_DATA_WIDTH-1:0] expected_now;

    // Clock generation.
    initial clka = 1'b0;
    always #(CLK_PERIOD_NS/2.0) clka = ~clka;

    // Device under test.
    rom_blackman_harris_1024 dut (
        .clka  (clka),
        .ena   (ena),
        .addra (addra),
        .douta (douta)
    );

    // ------------------------------------------------------------------------
    // Independent coefficient calculation
    //
    // Standard four-term Blackman-Harris window:
    //   w[n] = a0 - a1*cos(x) + a2*cos(2x) - a3*cos(3x)
    //   x    = 2*pi*n/(N-1)
    //
    // Q1.17 conversion matches the MATLAB generation rule:
    //   round(w[n] * 2^17), saturated to 18'h1ffff.
    // ------------------------------------------------------------------------
    function automatic [ROM_DATA_WIDTH-1:0] expected_coefficient;
        input integer address;
        real phase;
        real coefficient;
        real scaled_value;
        integer quantized_value;
        begin
            phase = 2.0 * 3.14159265358979323846 * address /
                    (ROM_DEPTH - 1.0);

            coefficient = 0.35875
                        - 0.48829 * $cos(phase)
                        + 0.14128 * $cos(2.0 * phase)
                        - 0.01168 * $cos(3.0 * phase);

            scaled_value    = coefficient * 131072.0;
            quantized_value = $rtoi(scaled_value + 0.5);

            if (quantized_value < 0)
                quantized_value = 0;
            else if (quantized_value > 131071)
                quantized_value = 131071;

            expected_coefficient = quantized_value[ROM_DATA_WIDTH-1:0];
        end
    endfunction

    // ------------------------------------------------------------------------
    // Request-tag pipeline
    //
    // At each rising edge, the ROM and stage 1 accept the same ena/addra.
    // One rising edge later, the tag reaches stage 2 and identifies the ROM
    // response that will be stable during that clock cycle.
    // ------------------------------------------------------------------------
    always @(posedge clka) begin
        rising_edge_count <= rising_edge_count + 1;

        request_valid_s1 <= ena;
        request_addr_s1  <= addra;

        response_valid_s2 <= request_valid_s1;
        response_addr_s2  <= request_addr_s1;

        if (ena && (addra < 8)) begin
            $display("E%0d STAGE1 REQUEST : addra=%0d accepted",
                     rising_edge_count + 1, addra);
        end
    end

    // ------------------------------------------------------------------------
    // Stable response checker
    //
    // Sampling on the falling edge gives the Xilinx ROM model much more than
    // its 100 ps clock-to-output delay. This avoids the previous erroneous
    // #0.001 sampling, which observed the old douta value.
    // ------------------------------------------------------------------------
    always @(negedge clka) begin
        if (response_valid_s2) begin
            expected_now = expected_coefficient(response_addr_s2);

            if (^douta === 1'bx) begin
                $error("ROM output contains X/Z: response address=%0d",
                       response_addr_s2);
                error_count = error_count + 1;
            end
            else if (douta !== expected_now) begin
                $error("ROM mismatch: address=%0d expected=%0d (0x%05h) got=%0d (0x%05h)",
                       response_addr_s2,
                       expected_now,
                       expected_now,
                       douta,
                       douta);
                error_count = error_count + 1;
            end

            if (captured_valid[response_addr_s2]) begin
                $error("ROM address %0d was captured more than once",
                       response_addr_s2);
                error_count = error_count + 1;
            end
            else begin
                captured_data[response_addr_s2]  = douta;
                captured_valid[response_addr_s2] = 1'b1;
                capture_count                    = capture_count + 1;
            end

            if (response_addr_s2 < 8) begin
                $display("E%0d STAGE2 RESPONSE: addr_s2=%0d douta=%0d expected=%0d",
                         rising_edge_count,
                         response_addr_s2,
                         douta,
                         expected_now);
            end
        end
    end

    // ------------------------------------------------------------------------
    // Stimulus and final checks
    // ------------------------------------------------------------------------
    initial begin
        ena               = 1'b0;
        addra             = {ROM_ADDR_WIDTH{1'b0}};
        request_valid_s1  = 1'b0;
        request_addr_s1   = {ROM_ADDR_WIDTH{1'b0}};
        response_valid_s2 = 1'b0;
        response_addr_s2  = {ROM_ADDR_WIDTH{1'b0}};
        capture_count     = 0;
        error_count       = 0;
        rising_edge_count = 0;
        expected_now      = {ROM_DATA_WIDTH{1'b0}};

        for (init_index = 0; init_index < ROM_DEPTH;
             init_index = init_index + 1) begin
            captured_data[init_index]  = {ROM_DATA_WIDTH{1'b0}};
            captured_valid[init_index] = 1'b0;
        end

        // Clear idle region before the first request.
        repeat (4) @(negedge clka);

        $display("------------------------------------------------------------");
        $display("Starting ROM test");
        $display("Configured read-latency stages: %0d", READ_LATENCY_STAGES);
        $display("Address is driven on falling edges and accepted on rising edges.");
        $display("Response is checked on falling edges after douta is stable.");
        $display("------------------------------------------------------------");

        // Present one address per clock. Falling-edge stimulus guarantees that
        // addra is stable before the ROM's active rising edge and avoids a
        // testbench/DUT race.
        for (drive_index = 0; drive_index < ROM_DEPTH;
             drive_index = drive_index + 1) begin
            @(negedge clka);
            ena   = 1'b1;
            addra = drive_index[ROM_ADDR_WIDTH-1:0];
        end

        // Stop issuing requests and flush the final response.
        @(negedge clka);
        ena = 1'b0;

        repeat (READ_LATENCY_STAGES + 3) @(negedge clka);

        if (capture_count != ROM_DEPTH) begin
            $error("Expected %0d ROM responses, captured %0d",
                   ROM_DEPTH, capture_count);
            error_count = error_count + 1;
        end

        for (init_index = 0; init_index < ROM_DEPTH;
             init_index = init_index + 1) begin
            if (!captured_valid[init_index]) begin
                $error("No response was captured for ROM address %0d",
                       init_index);
                error_count = error_count + 1;
            end
        end

        $display("------------------------------------------------------------");
        $display("ROM[0]    = %0d", captured_data[0]);
        $display("ROM[1]    = %0d", captured_data[1]);
        $display("ROM[2]    = %0d", captured_data[2]);
        $display("ROM[3]    = %0d", captured_data[3]);
        $display("ROM[4]    = %0d", captured_data[4]);
        $display("ROM[511]  = %0d", captured_data[511]);
        $display("ROM[512]  = %0d", captured_data[512]);
        $display("ROM[1023] = %0d", captured_data[1023]);
        $display("Captured responses = %0d", capture_count);

        if (error_count == 0) begin
            $display("PASS: all 1024 ROM coefficients and the two-stage response alignment are correct.");
        end
        else begin
            $fatal(1, "FAIL: ROM test completed with %0d error(s).",
                   error_count);
        end

        $display("------------------------------------------------------------");
        $finish;
    end

endmodule
