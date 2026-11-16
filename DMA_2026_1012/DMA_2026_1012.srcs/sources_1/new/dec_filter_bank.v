`timescale 1ns / 1ps

// ============================================================================
// 模块名称：dec_filter_bank
// 功能说明：
//   1. 四路并行输入先经过多相FIR，合成为一路16位数据；
//   2. CIC完成4～8192倍可编程抽取，并根据配置完成32位舍入截位；
//   3. HB完成0～6级可选半带抽取，对应1～64倍倍率；
//   4. 将CIC配置忙状态向上层输出，供PS查询和切换流程控制。
//
// 宽带直通：
//   cic_rate_cfg == 1 表示旁路CIC。此时多相滤波器输出直接送入HB，
//   HB仍可选择0～6级抽取。CIC=1/HB=0时FFT输入为312.5 MSPS。
//
// 默认配置由CIC_filter和上层寄存器共同确定：
//   - CIC默认抽取倍率为8；
//   - CIC默认最高截位为31；
//   - HB建议默认选择2级，即4倍抽取，以保持原工程总抽取倍率。
//
// 使用约束：
//   - cic_cfg_valid应在cic_cfg_busy=0时产生一个clk周期的高脉冲；
//   - cic_rate_cfg、cic_trunc_cfg和hb_level应在配置切换期间保持稳定；
//   - 建议只在帧边界或数据源暂停时修改滤波参数。
// ============================================================================

module dec_filter_bank (
    input  wire                    clk,
    input  wire                    rst_n,

    input  wire signed [13:0]      ch0_data,
    input  wire signed [13:0]      ch1_data,
    input  wire signed [13:0]      ch2_data,
    input  wire signed [13:0]      ch3_data,
    input  wire                    data_in_valid,

    // CIC可编程配置。
    input  wire [15:0]             cic_rate_cfg,
    input  wire [7:0]              cic_trunc_cfg,
    input  wire                    cic_cfg_valid,
    output wire                    cic_cfg_busy,

    // HB级数：0为旁路，1～6分别对应2～64倍抽取。
    input  wire [2:0]              hb_level,

    output wire signed [31:0]      data_out,
    output wire                    data_out_valid
);

// ----------------------------------------------------------------------------
// 四相多相FIR：四路14位并行输入合成为一路16位输出
// ----------------------------------------------------------------------------
wire signed [15:0] poly_data_out;
wire               poly_data_valid;

Poly_filter u_poly_filter (
    .clk             (clk),
    .rst_n           (rst_n),
    .ch0_data        (ch0_data),
    .ch1_data        (ch1_data),
    .ch2_data        (ch2_data),
    .ch3_data        (ch3_data),
    .data_in_valid   (data_in_valid),
    .data_out        (poly_data_out),
    .data_out_valid  (poly_data_valid)
);

// ----------------------------------------------------------------------------
// CIC旁路
//
// 多相滤波器输出是16位有符号数据。CIC正常输出是32位，因此旁路时把
// 16位样本左移16位，保持与CIC满量程32位数据相近的FFT输入幅度。
// ----------------------------------------------------------------------------
wire cic_bypass = (cic_rate_cfg == 16'd1);
wire signed [31:0] bypass_data_out = {poly_data_out, 16'd0};
wire               bypass_data_valid = poly_data_valid;

reg [2:0] bypass_cfg_ack;
always @(posedge clk or negedge rst_n) begin
    if (!rst_n)
        bypass_cfg_ack <= 3'd0;
    else if (cic_bypass && cic_cfg_valid)
        bypass_cfg_ack <= 3'd4;
    else if (bypass_cfg_ack != 3'd0)
        bypass_cfg_ack <= bypass_cfg_ack - 1'b1;
end

wire bypass_cfg_busy = (bypass_cfg_ack != 3'd0);
wire cic_cfg_valid_i = cic_cfg_valid && !cic_bypass;

// ----------------------------------------------------------------------------
// 可编程CIC抽取滤波器
// ----------------------------------------------------------------------------
wire signed [31:0] cic_data_out;
wire               cic_data_valid;
wire               cic_cfg_busy_i;

CIC_filter u_cic_filter (
    .clk             (clk),
    .rst_n           (rst_n),

    .data_in         (poly_data_out),
    .data_in_valid   (poly_data_valid),

    .cic_rate_cfg    (cic_rate_cfg),
    .cic_trunc_cfg   (cic_trunc_cfg),
    .cic_cfg_valid   (cic_cfg_valid_i),
    .cic_cfg_busy    (cic_cfg_busy_i),

    .data_out        (cic_data_out),
    .data_out_valid  (cic_data_valid)
);

// ----------------------------------------------------------------------------
// 可变级数HB抽取滤波器
// ----------------------------------------------------------------------------
wire signed [31:0] hb_data_out;
wire               hb_data_valid;
wire signed [31:0] cic_data_selected =
    cic_bypass ? bypass_data_out : cic_data_out;
wire cic_data_valid_selected =
    cic_bypass ? bypass_data_valid : cic_data_valid;

HB_filter u_hb_filter (
    .clk             (clk),
    .rst_n           (rst_n),
    .hb_level        (hb_level),
    .data_in         (cic_data_selected),
    .data_in_valid   (cic_data_valid_selected),
    .data_out        (hb_data_out),
    .data_out_valid  (hb_data_valid)
);

assign data_out       = hb_data_out;
assign data_out_valid = hb_data_valid;
assign cic_cfg_busy  = cic_bypass ? bypass_cfg_busy : cic_cfg_busy_i;

// ----------------------------------------------------------------------------
// 调试观测
//
// 沿用原工程的ILA探针宽度与连接顺序，不需要重新配置ILA IP：
//   probe0：多相FIR输出数据；
//   probe1：多相FIR输出有效；
//   probe2：CIC输出数据；
//   probe3：CIC输出有效；
//   probe4：HB输出数据；
//   probe5：HB输出有效。
// ----------------------------------------------------------------------------
ila_1 u_ila_dec_filter (
    .clk     (clk),
    .probe0  (poly_data_out),
    .probe1  (poly_data_valid),
    .probe2  (cic_data_out),
    .probe3  (cic_data_valid),
    .probe4  (hb_data_out),
    .probe5  (hb_data_valid)
);

endmodule

