`timescale 1ns / 1ps

// Fast integration test for the CIC bypass path in dec_filter_bank.
// It uses the real Poly/CIC/HB IP blocks from the Vivado project.
module tb_dec_bypass;

reg clk = 1'b0;
reg rst_n = 1'b0;

reg signed [13:0] ch0_data = 14'sd0;
reg signed [13:0] ch1_data = 14'sd1;
reg signed [13:0] ch2_data = 14'sd2;
reg signed [13:0] ch3_data = 14'sd3;
reg               data_in_valid = 1'b0;

reg [15:0] cic_rate_cfg = 16'd1;
reg [7:0]  cic_trunc_cfg = 8'd31;
reg        cic_cfg_valid = 1'b0;
wire       cic_cfg_busy;
reg [2:0]  hb_level = 3'd0;

wire signed [31:0] data_out;
wire               data_out_valid;

integer valid_count = 0;
integer nonzero_count = 0;
integer i;

always #1.6 clk = ~clk;

always @(posedge clk) begin
    if (!rst_n) begin
        ch0_data <= 14'sd0;
        ch1_data <= 14'sd1;
        ch2_data <= 14'sd2;
        ch3_data <= 14'sd3;
    end else if (data_in_valid) begin
        ch0_data <= ch0_data + 14'sd4;
        ch1_data <= ch1_data + 14'sd4;
        ch2_data <= ch2_data + 14'sd4;
        ch3_data <= ch3_data + 14'sd4;
    end
end

always @(posedge clk) begin
    if (rst_n && data_out_valid) begin
        valid_count = valid_count + 1;
        if (data_out != 0)
            nonzero_count = nonzero_count + 1;
    end
end

dec_filter_bank dut (
    .clk             (clk),
    .rst_n           (rst_n),
    .ch0_data        (ch0_data),
    .ch1_data        (ch1_data),
    .ch2_data        (ch2_data),
    .ch3_data        (ch3_data),
    .data_in_valid   (data_in_valid),
    .cic_rate_cfg    (cic_rate_cfg),
    .cic_trunc_cfg   (cic_trunc_cfg),
    .cic_cfg_valid   (cic_cfg_valid),
    .cic_cfg_busy    (cic_cfg_busy),
    .hb_level        (hb_level),
    .data_out        (data_out),
    .data_out_valid  (data_out_valid)
);

task apply_filter_config;
    input [15:0] rate;
    input [7:0]  trunc;
    input [2:0]  hb;
    begin
        @(negedge clk);
        data_in_valid = 1'b0;
        while (cic_cfg_busy !== 1'b0)
            @(negedge clk);
        cic_rate_cfg  = rate;
        cic_trunc_cfg = trunc;
        hb_level      = hb;
        @(negedge clk);
        cic_cfg_valid = 1'b1;
        @(negedge clk);
        cic_cfg_valid = 1'b0;
        while (cic_cfg_busy !== 1'b1)
            @(negedge clk);
        while (cic_cfg_busy !== 1'b0)
            @(negedge clk);
        repeat (8) @(negedge clk);
        data_in_valid = 1'b1;
    end
endtask

initial begin
    repeat (10) @(negedge clk);
    rst_n = 1'b1;
    repeat (10) @(negedge clk);

    // CIC=1, HB=0: bypass, one valid per clock after pipeline fill.
    apply_filter_config(16'd1, 8'd31, 3'd0);
    repeat (40) @(negedge clk);
    valid_count = 0;
    nonzero_count = 0;
    repeat (512) @(negedge clk);
    if (valid_count < 480) begin
        $error("bypass valid rate too low: %0d/512", valid_count);
        $fatal(1);
    end
    if (nonzero_count < 100) begin
        $error("bypass output stayed near zero: %0d/512", nonzero_count);
        $fatal(1);
    end
    $display("PASS: CIC bypass valid rate %0d/512", valid_count);

    // CIC=4, HB=0: normal decimation, roughly one valid per 4 clocks.
    apply_filter_config(16'd4, 8'd31, 3'd0);
    repeat (40) @(negedge clk);
    valid_count = 0;
    repeat (512) @(negedge clk);
    if (valid_count < 100 || valid_count > 160) begin
        $error("normal CIC valid rate unexpected: %0d/512", valid_count);
        $fatal(1);
    end
    $display("PASS: CIC=4 valid rate %0d/512", valid_count);

    $finish;
end

initial begin
    #200000;
    $fatal(1, "timeout");
end

endmodule
