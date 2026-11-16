`timescale 1ns / 1ps

//==============================================================================
// frequency_mask_trigger测试文件
//
// 验证内容：
//   1. FFT第123频点功率超过对应模板时，能够正确触发；
//   2. 提高第123频点门限后，相同FFT数据不再触发；
//   3. FFT第700频点使用负实部数据时，功率和频点仍能正确计算；
//   4. fft_tready暂停期间，频点计数不会继续增加。
//==============================================================================
module tb_frequency_mask_trigger_modified;

localparam real CLK_PERIOD = 3.2;

reg         clk;
reg         rst;
reg  [63:0] fft_tdata;
reg         fft_tvalid;
reg         fft_tready;
reg         fft_tlast;
reg         trigger_enable;
reg         trigger_clear;
reg         mask_wr_en;
reg  [9:0]  mask_wr_addr;
reg  [63:0] mask_wr_data;

wire        trigger_pulse;
wire        trigger_latched;
wire [9:0]  trigger_bin;
wire [63:0] trigger_power;
wire [63:0] trigger_threshold;

frequency_mask_trigger dut (
    .clk               (clk),
    .rst               (rst),
    .fft_tdata         (fft_tdata),
    .fft_tvalid        (fft_tvalid),
    .fft_tready        (fft_tready),
    .fft_tlast         (fft_tlast),
    .trigger_enable    (trigger_enable),
    .trigger_clear     (trigger_clear),
    .mask_wr_en        (mask_wr_en),
    .mask_wr_addr      (mask_wr_addr),
    .mask_wr_data      (mask_wr_data),
    .trigger_pulse     (trigger_pulse),
    .trigger_latched   (trigger_latched),
    .trigger_bin       (trigger_bin),
    .trigger_power     (trigger_power),
    .trigger_threshold (trigger_threshold)
);

initial begin
    clk = 1'b0;
    forever #(CLK_PERIOD / 2.0) clk = ~clk;
end

// 统计触发脉冲数量，下降沿观察可以避开时钟上升沿的赋值竞争。
integer pulse_count;

always @(negedge clk) begin
    if (trigger_pulse) begin
        pulse_count = pulse_count + 1;
        $display("触发：bin=%0d, power=%0d, threshold=%0d, time=%0t",
                 trigger_bin, trigger_power, trigger_threshold, $time);
    end
end

//------------------------------------------------------------------------------
// 将1024个频点的门限全部初始化为最大值，即默认不触发。
//------------------------------------------------------------------------------
task automatic initialize_mask;
    integer k;
    begin
        for (k = 0; k < 1024; k = k + 1) begin
            @(negedge clk);
            mask_wr_en   = 1'b1;
            mask_wr_addr = k[9:0];
            mask_wr_data = 64'hFFFF_FFFF_FFFF_FFFF;
        end

        @(negedge clk);
        mask_wr_en   = 1'b0;
        mask_wr_addr = 10'd0;
        mask_wr_data = 64'd0;
    end
endtask

// 修改某一个频点的功率门限。
task automatic write_one_mask(
    input [9:0]  addr,
    input [63:0] threshold
);
    begin
        @(negedge clk);
        mask_wr_en   = 1'b1;
        mask_wr_addr = addr;
        mask_wr_data = threshold;

        @(negedge clk);
        mask_wr_en   = 1'b0;
    end
endtask

// 清除锁存的触发状态，使模块能够响应下一次触发。
task automatic clear_trigger_state;
    begin
        @(negedge clk);
        trigger_clear = 1'b1;
        @(negedge clk);
        trigger_clear = 1'b0;
    end
endtask

//------------------------------------------------------------------------------
// 发送一帧1024点FFT数据。
// 除target_bin外，其余频点均输入0。
// 当add_ready_stall为1时，在第50频点人为暂停ready三个时钟周期。
//------------------------------------------------------------------------------
task automatic send_fft_frame(
    input integer               target_bin,
    input logic signed [31:0]   target_real,
    input logic signed [31:0]   target_imag,
    input bit                   add_ready_stall
);
    integer k;
    begin
        fft_tready = 1'b1;

        for (k = 0; k < 1024; k = k + 1) begin
            @(negedge clk);
            fft_tvalid = 1'b1;
            fft_tlast  = (k == 1023);

            if (k == target_bin)
                fft_tdata = {target_imag, target_real};
            else
                fft_tdata = 64'd0;

            // 暂停期间保持当前FFT数据和valid不变。
            if (add_ready_stall && (k == 50)) begin
                fft_tready = 1'b0;
                repeat (3) @(negedge clk);
                fft_tready = 1'b1;
            end

            // 在下一个上升沿完成一次valid/ready握手。
            @(posedge clk);
        end

        @(negedge clk);
        fft_tvalid = 1'b0;
        fft_tlast  = 1'b0;
        fft_tdata  = 64'd0;
        fft_tready = 1'b1;
    end
endtask

initial begin
    rst               = 1'b1;
    fft_tdata         = 64'd0;
    fft_tvalid        = 1'b0;
    fft_tready        = 1'b1;
    fft_tlast         = 1'b0;
    trigger_enable    = 1'b0;
    trigger_clear     = 1'b0;
    mask_wr_en        = 1'b0;
    mask_wr_addr      = 10'd0;
    mask_wr_data      = 64'd0;
    pulse_count       = 0;

    repeat (10) @(posedge clk);
    @(negedge clk);
    rst = 1'b0;

    // 先初始化整张频率模板。
    initialize_mask();

    //--------------------------------------------------------------------------
    // 测试1：第123频点输入(40,30)，功率为40^2+30^2=2500。
    // 门限设置为2000，应当在第123频点触发。
    //--------------------------------------------------------------------------
    write_one_mask(10'd123, 64'd2000);
    trigger_enable = 1'b1;
    send_fft_frame(123, 32'sd40, 32'sd30, 1'b1);
    repeat (10) @(posedge clk);

    if (!trigger_latched || (pulse_count != 1) ||
        (trigger_bin != 10'd123) ||
        (trigger_power != 64'd2500) ||
        (trigger_threshold != 64'd2000)) begin
        $fatal(1, "测试1失败：第123频点没有按预期触发");
    end
    $display("测试1通过：第123频点超过模板并正确触发");

    //--------------------------------------------------------------------------
    // 测试2：把第123频点门限提高到3000。
    // 相同功率2500小于门限，因此不应触发。
    //--------------------------------------------------------------------------
    trigger_enable = 1'b0;
    clear_trigger_state();
    write_one_mask(10'd123, 64'd3000);
    trigger_enable = 1'b1;
    send_fft_frame(123, 32'sd40, 32'sd30, 1'b0);
    repeat (10) @(posedge clk);

    if (trigger_latched || (pulse_count != 1)) begin
        $fatal(1, "测试2失败：低于模板门限时产生了错误触发");
    end
    $display("测试2通过：第123频点低于模板，没有触发");

    //--------------------------------------------------------------------------
    // 测试3：第700频点输入(-40,30)，功率仍为2500。
    // 门限设置为2000，应在第700频点触发。
    //--------------------------------------------------------------------------
    trigger_enable = 1'b0;
    write_one_mask(10'd700, 64'd2000);
    trigger_enable = 1'b1;
    send_fft_frame(700, -32'sd40, 32'sd30, 1'b0);
    repeat (10) @(posedge clk);

    if (!trigger_latched || (pulse_count != 2) ||
        (trigger_bin != 10'd700) ||
        (trigger_power != 64'd2500) ||
        (trigger_threshold != 64'd2000)) begin
        $fatal(1, "测试3失败：第700频点没有按预期触发");
    end
    $display("测试3通过：负数FFT数据的平方和频点对应关系正确");

    $display("PASS：频率模板触发模块全部测试通过");
    $finish;
end

endmodule

