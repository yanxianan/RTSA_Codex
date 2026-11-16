`timescale 1ns / 1ps

// ============================================================================
// 模块名称：tb_dec_filter_bank_modified
//
// 仿真目的：
//   1. 给完整抽取滤波器组提供连续的四路输入数据；
//   2. 依次配置4组不同的CIC倍率和HB级数；
//   3. 每组配置保持较长时间，便于直接观察data_out和data_out_valid；
//   4. 不在TB中建立复杂数学模型，主要通过波形判断滤波器是否工作。
//
// 建议观察信号：
//   data_in_valid、cic_rate_cfg、cic_trunc_cfg、cic_cfg_valid、cic_cfg_busy、
//   hb_level、data_out、data_out_valid。
//
// 四组配置对应的理论最终抽取倍率：
//   CIC=4， HB=0：4倍；
//   CIC=4， HB=1：8倍；
//   CIC=16，HB=2：64倍；
//   CIC=64，HB=3：512倍。
// ============================================================================

module tb_dec_filter_bank;

// 312.5 MHz时钟，周期为3.2 ns。
reg clk;
reg rst_n;

reg signed [13:0] ch0_data;
reg signed [13:0] ch1_data;
reg signed [13:0] ch2_data;
reg signed [13:0] ch3_data;
reg               data_in_valid;

reg  [15:0] cic_rate_cfg;
reg  [7:0]  cic_trunc_cfg;
reg         cic_cfg_valid;
wire        cic_cfg_busy;

reg  [2:0]  hb_level;

wire signed [31:0] data_out;
wire               data_out_valid;

reg [13:0] sample_counter;

// 每组配置保持10000个时钟周期，约为32 us。
localparam integer HOLD_CLOCKS = 10000;

// ----------------------------------------------------------------------------
// 被测的完整抽取滤波器组
// ----------------------------------------------------------------------------
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

// ----------------------------------------------------------------------------
// 时钟
// ----------------------------------------------------------------------------
initial begin
    clk = 1'b0;
    forever #1.6 clk = ~clk;
end

// ----------------------------------------------------------------------------
// 四路连续输入
//
// 四个端口表示同一条高速采样流中连续的4个样点：
//   ch0=n，ch1=n+1，ch2=n+2，ch3=n+3；
// 下一拍从n+4继续。数据在14位范围内自然回绕。
// ----------------------------------------------------------------------------
always @(posedge clk) begin
    if (!rst_n) begin
        sample_counter <= 14'd0;
        ch0_data       <= 14'sd0;
        ch1_data       <= 14'sd1;
        ch2_data       <= 14'sd2;
        ch3_data       <= 14'sd3;
    end
    else if (data_in_valid) begin
        ch0_data       <= sample_counter;
        ch1_data       <= sample_counter + 14'd1;
        ch2_data       <= sample_counter + 14'd2;
        ch3_data       <= sample_counter + 14'd3;
        sample_counter <= sample_counter + 14'd4;
    end
end

// ----------------------------------------------------------------------------
// 修改CIC和HB配置
//
// 配置期间暂停输入；等待CIC完成重新配置后，再恢复连续数据。
// ----------------------------------------------------------------------------
task change_filter_config;
    input [15:0] new_cic_rate;
    input [7:0]  new_cic_trunc;
    input [2:0]  new_hb_level;
    begin
        @(negedge clk);
        data_in_valid = 1'b0;

        // 必须等待上一轮CIC配置结束。
        while (cic_cfg_busy !== 1'b0)
            @(negedge clk);

        cic_rate_cfg  = new_cic_rate;
        cic_trunc_cfg = new_cic_trunc;
        hb_level      = new_hb_level;

        // cic_cfg_valid保持一个完整时钟周期。
        @(negedge clk);
        cic_cfg_valid = 1'b1;
        @(negedge clk);
        cic_cfg_valid = 1'b0;

        // 等待CIC进入忙状态，再等待配置完成。
        while (cic_cfg_busy !== 1'b1)
            @(negedge clk);
        while (cic_cfg_busy !== 1'b0)
            @(negedge clk);

        // 给HB档位切换和内部复位留出额外时间。
        repeat (10) @(negedge clk);
        data_in_valid = 1'b1;

        $display("时间%0t ns：CIC=%0d，TRUNC=%0d，HB_LEVEL=%0d，开始连续输入。",
                 $time, new_cic_rate, new_cic_trunc, new_hb_level);
    end
endtask

// ----------------------------------------------------------------------------
// 主测试流程
// ----------------------------------------------------------------------------
initial begin
    rst_n           = 1'b0;
    data_in_valid   = 1'b0;
    cic_rate_cfg    = 16'd4;
    cic_trunc_cfg   = 8'd25;
    cic_cfg_valid   = 1'b0;
    hb_level        = 3'd0;
    sample_counter  = 14'd0;
    ch0_data        = 14'sd0;
    ch1_data        = 14'sd1;
    ch2_data        = 14'sd2;
    ch3_data        = 14'sd3;

    // 复位保持20个时钟周期。
    repeat (20) @(negedge clk);
    rst_n = 1'b1;

    // 第1组：CIC 4倍，HB旁路，最终4倍抽取。
    change_filter_config(16'd4, 8'd25, 3'd0);
    repeat (HOLD_CLOCKS) @(negedge clk);

    // 第2组：CIC 4倍，HB 1级，最终8倍抽取。
    change_filter_config(16'd4, 8'd25, 3'd1);
    repeat (HOLD_CLOCKS) @(negedge clk);

    // 第3组：CIC 16倍，HB 2级，最终64倍抽取。
    change_filter_config(16'd16, 8'd35, 3'd2);
    repeat (HOLD_CLOCKS) @(negedge clk);

    // 第4组：CIC 64倍，HB 3级，最终512倍抽取。
    change_filter_config(16'd64, 8'd45, 3'd3);
    repeat (HOLD_CLOCKS) @(negedge clk);

    data_in_valid = 1'b0;
    repeat (20) @(negedge clk);

    $display("四组抽取配置仿真结束，请结合data_out_valid观察输出间隔和data_out波形。");
    $finish;
end

endmodule

