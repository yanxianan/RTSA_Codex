`timescale 1ns / 1ps

//==============================================================================
// 1024点频段功率触发模块
//
// 功能：
//   1. 只监测start_bin到end_bin之间的FFT频点；
//   2. 计算每个被监测频点的功率：实部^2 + 虚部^2；
//   3. 功率大于power_threshold时产生一个时钟周期的触发脉冲；
//   4. 锁存触发频点、实际功率和门限，直到trigger_clear有效。
//
// FFT数据格式：
//   fft_tdata[31:0]  ：32位有符号实部
//   fft_tdata[63:32] ：32位有符号虚部
//==============================================================================
module frequency_mask_trigger (
    input  wire         clk,
    input  wire         rst,

    // FFT AXI-Stream输出。本模块只观察数据，不控制fft_tready。
    input  wire [63:0]  fft_tdata,
    input  wire         fft_tvalid,
    input  wire         fft_tready,
    input  wire         fft_tlast,

    // 触发配置。配置期间建议先令trigger_enable为0。
    input  wire         trigger_enable,
    input  wire         trigger_clear,
    input  wire [9:0]   start_bin,
    input  wire [9:0]   end_bin,
    input  wire [63:0]  power_threshold,

    // 触发结果
    output reg          trigger_pulse,
    output reg          trigger_latched,
    output reg  [9:0]   trigger_bin,
    output reg  [63:0]  trigger_power,
    output reg  [63:0]  trigger_threshold
);

// 只有valid和ready同时为1，FFT才真正传输了一个频点。
wire fft_transfer = fft_tvalid && fft_tready;

// 当前FFT频点编号。FFT采用自然顺序输出，因此依次为0～1023。
reg [9:0] fft_bin_count;

always @(posedge clk) begin
    if (rst) begin
        fft_bin_count <= 10'd0;
    end
    else if (fft_transfer) begin
        // 正常情况下在第1023点收到tlast；频点1023作为保护条件。
        if (fft_tlast || (fft_bin_count == 10'd1023))
            fft_bin_count <= 10'd0;
        else
            fft_bin_count <= fft_bin_count + 1'b1;
    end
end

//------------------------------------------------------------------------------
// 第1级：接收FFT数据，并记录该数据对应的频点、门限和频段判断结果。
//------------------------------------------------------------------------------
reg signed [31:0] fft_real_s1;
reg signed [31:0] fft_imag_s1;
reg        [63:0] threshold_s1;
reg        [9:0]  bin_s1;
reg               in_range_s1;
reg               valid_s1;

always @(posedge clk) begin
    if (rst) begin
        fft_real_s1  <= 32'sd0;
        fft_imag_s1  <= 32'sd0;
        threshold_s1 <= 64'd0;
        bin_s1       <= 10'd0;
        in_range_s1  <= 1'b0;
        valid_s1     <= 1'b0;
    end
    else begin
        valid_s1 <= fft_transfer;

        if (fft_transfer) begin
            fft_real_s1  <= $signed(fft_tdata[31:0]);
            fft_imag_s1  <= $signed(fft_tdata[63:32]);
            threshold_s1 <= power_threshold;
            bin_s1       <= fft_bin_count;
            in_range_s1  <= (fft_bin_count >= start_bin) &&
                            (fft_bin_count <= end_bin);
        end
    end
end

//------------------------------------------------------------------------------
// 第2级：分别计算实部平方和虚部平方。
// use_dsp属性提示Vivado优先使用DSP资源实现乘法。
//------------------------------------------------------------------------------
(* use_dsp = "yes" *) reg [63:0] real_square_s2;
(* use_dsp = "yes" *) reg [63:0] imag_square_s2;
reg [63:0] threshold_s2;
reg [9:0]  bin_s2;
reg        in_range_s2;
reg        valid_s2;

always @(posedge clk) begin
    if (rst) begin
        real_square_s2 <= 64'd0;
        imag_square_s2 <= 64'd0;
        threshold_s2   <= 64'd0;
        bin_s2         <= 10'd0;
        in_range_s2    <= 1'b0;
        valid_s2       <= 1'b0;
    end
    else begin
        valid_s2 <= valid_s1;

        if (valid_s1) begin
            real_square_s2 <= fft_real_s1 * fft_real_s1;
            imag_square_s2 <= fft_imag_s1 * fft_imag_s1;
            threshold_s2   <= threshold_s1;
            bin_s2         <= bin_s1;
            in_range_s2    <= in_range_s1;
        end
    end
end

//------------------------------------------------------------------------------
// 第3级：两个平方值相加，得到当前频点的线性功率。
//------------------------------------------------------------------------------
reg [63:0] power_s3;
reg [63:0] threshold_s3;
reg [9:0]  bin_s3;
reg        in_range_s3;
reg        valid_s3;

always @(posedge clk) begin
    if (rst) begin
        power_s3     <= 64'd0;
        threshold_s3 <= 64'd0;
        bin_s3       <= 10'd0;
        in_range_s3  <= 1'b0;
        valid_s3     <= 1'b0;
    end
    else begin
        valid_s3 <= valid_s2;

        if (valid_s2) begin
            power_s3     <= real_square_s2 + imag_square_s2;
            threshold_s3 <= threshold_s2;
            bin_s3       <= bin_s2;
            in_range_s3  <= in_range_s2;
        end
    end
end

//------------------------------------------------------------------------------
// 第4级：只比较指定频段内的功率。
// trigger_pulse只保持一个时钟周期；trigger_latched保持到软件清除。
//------------------------------------------------------------------------------
always @(posedge clk) begin
    if (rst) begin
        trigger_pulse     <= 1'b0;
        trigger_latched   <= 1'b0;
        trigger_bin       <= 10'd0;
        trigger_power     <= 64'd0;
        trigger_threshold <= 64'd0;
    end
    else begin
        trigger_pulse <= 1'b0;

        if (trigger_clear) begin
            trigger_latched <= 1'b0;
        end
        else if (trigger_enable && valid_s3 && in_range_s3 &&
                 (power_s3 > threshold_s3) && !trigger_latched) begin
            trigger_pulse     <= 1'b1;
            trigger_latched   <= 1'b1;
            trigger_bin       <= bin_s3;
            trigger_power     <= power_s3;
            trigger_threshold <= threshold_s3;
        end
    end
end

endmodule

