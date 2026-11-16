`timescale 1ns / 1ps

// ============================================================================
// 模块名称：HB_filter
// 功能说明：
//   1. 使用5个 halfband_filter 和1个 last_hb 构成可变级数半带抽取链；
//   2. hb_level=0时旁路HB，hb_level=1～6时分别完成2～64倍抽取；
//   3. 每一级都把FIR全精度结果舍入并恢复为32位，供下一级继续处理；
//   4. 检测到hb_level改变后，自动复位全部FIR流水线，避免不同档位的数据混合。
//
// 当前IP接口约定：
//   - halfband_filter：32位输入、48位全精度输出、2倍抽取；
//   - last_hb：32位输入、49位有效结果，AXI端口补齐为56位、2倍抽取；
//   - 两个IP均按312.5 MHz时钟和312.5 MHz最大样点速率配置。
//
// 使用约束：
//   - hb_level应尽量只在帧边界改变；
//   - 档位改变后的复位期间，data_out_valid保持为0；
//   - hb_level=7属于非法值，模块会自动按6级处理。
// ============================================================================

module HB_filter (
    input  wire                    clk,
    input  wire                    rst_n,

    // 0：旁路；1～6：启用对应级数的2倍半带抽取。
    input  wire [2:0]              hb_level,

    input  wire signed [31:0]      data_in,
    input  wire                    data_in_valid,

    output wire signed [31:0]      data_out,
    output wire                    data_out_valid
);

// ----------------------------------------------------------------------------
// 档位锁存与流水线复位
// ----------------------------------------------------------------------------
wire [2:0] hb_level_limited = (hb_level > 3'd6) ? 3'd6 : hb_level;

reg  [2:0] active_hb_level;
reg  [1:0] reset_count;
reg        fir_aresetn;

always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
        active_hb_level <= 3'd0;
        reset_count     <= 2'd0;
        fir_aresetn     <= 1'b0;
    end
    else if (hb_level_limited != active_hb_level) begin
        // 档位改变后先锁存新档位，并清空全部FIR内部状态。
        active_hb_level <= hb_level_limited;
        reset_count     <= 2'd2;
        fir_aresetn     <= 1'b0;
    end
    else if (reset_count != 2'd0) begin
        reset_count <= reset_count - 1'b1;
        fir_aresetn <= 1'b0;
    end
    else begin
        fir_aresetn <= 1'b1;
    end
end

// ----------------------------------------------------------------------------
// 五级普通半带抽取链
//
// 每级FIR输出为48位。按照杜威原工程的定点格式，在截去15个低位前
// 加入2^14-1的补偿值，然后保留bit46～bit15作为下一阶段的32位输入。
// 舍入和截位各使用一级输出寄存器，valid与数据同步延迟。
// ----------------------------------------------------------------------------
localparam signed [47:0] HB_ROUND_COMP_48 = 48'sd16383;

wire signed [47:0] hb_raw_0;
wire signed [47:0] hb_raw_1;
wire signed [47:0] hb_raw_2;
wire signed [47:0] hb_raw_3;
wire signed [47:0] hb_raw_4;

wire hb_raw_valid_0;
wire hb_raw_valid_1;
wire hb_raw_valid_2;
wire hb_raw_valid_3;
wire hb_raw_valid_4;

wire hb_ready_0;
wire hb_ready_1;
wire hb_ready_2;
wire hb_ready_3;
wire hb_ready_4;

wire signed [47:0] hb_rounded_0 = hb_raw_0 + HB_ROUND_COMP_48;
wire signed [47:0] hb_rounded_1 = hb_raw_1 + HB_ROUND_COMP_48;
wire signed [47:0] hb_rounded_2 = hb_raw_2 + HB_ROUND_COMP_48;
wire signed [47:0] hb_rounded_3 = hb_raw_3 + HB_ROUND_COMP_48;
wire signed [47:0] hb_rounded_4 = hb_raw_4 + HB_ROUND_COMP_48;

reg signed [31:0] hb_data_0;
reg signed [31:0] hb_data_1;
reg signed [31:0] hb_data_2;
reg signed [31:0] hb_data_3;
reg signed [31:0] hb_data_4;

reg hb_valid_0;
reg hb_valid_1;
reg hb_valid_2;
reg hb_valid_3;
reg hb_valid_4;

always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
        hb_data_0  <= 32'sd0;
        hb_data_1  <= 32'sd0;
        hb_data_2  <= 32'sd0;
        hb_data_3  <= 32'sd0;
        hb_data_4  <= 32'sd0;
        hb_valid_0 <= 1'b0;
        hb_valid_1 <= 1'b0;
        hb_valid_2 <= 1'b0;
        hb_valid_3 <= 1'b0;
        hb_valid_4 <= 1'b0;
    end
    else if (!fir_aresetn) begin
        hb_data_0  <= 32'sd0;
        hb_data_1  <= 32'sd0;
        hb_data_2  <= 32'sd0;
        hb_data_3  <= 32'sd0;
        hb_data_4  <= 32'sd0;
        hb_valid_0 <= 1'b0;
        hb_valid_1 <= 1'b0;
        hb_valid_2 <= 1'b0;
        hb_valid_3 <= 1'b0;
        hb_valid_4 <= 1'b0;
    end
    else begin
        hb_valid_0 <= hb_raw_valid_0;
        hb_valid_1 <= hb_raw_valid_1;
        hb_valid_2 <= hb_raw_valid_2;
        hb_valid_3 <= hb_raw_valid_3;
        hb_valid_4 <= hb_raw_valid_4;

        if (hb_raw_valid_0)
            hb_data_0 <= hb_rounded_0[46:15];
        if (hb_raw_valid_1)
            hb_data_1 <= hb_rounded_1[46:15];
        if (hb_raw_valid_2)
            hb_data_2 <= hb_rounded_2[46:15];
        if (hb_raw_valid_3)
            hb_data_3 <= hb_rounded_3[46:15];
        if (hb_raw_valid_4)
            hb_data_4 <= hb_rounded_4[46:15];
    end
end

// 只有选择2级及以上HB时，才需要启动前面的普通半带链。
wire hb_chain_input_valid = data_in_valid &&
                            fir_aresetn &&
                            (active_hb_level >= 3'd2);

halfband_filter u_halfband_0 (
    .aclk                (clk),
    .aresetn             (fir_aresetn),
    .s_axis_data_tvalid  (hb_chain_input_valid),
    .s_axis_data_tready  (hb_ready_0),
    .s_axis_data_tdata   (data_in),
    .m_axis_data_tvalid  (hb_raw_valid_0),
    .m_axis_data_tdata   (hb_raw_0)
);

halfband_filter u_halfband_1 (
    .aclk                (clk),
    .aresetn             (fir_aresetn),
    .s_axis_data_tvalid  (hb_valid_0),
    .s_axis_data_tready  (hb_ready_1),
    .s_axis_data_tdata   (hb_data_0),
    .m_axis_data_tvalid  (hb_raw_valid_1),
    .m_axis_data_tdata   (hb_raw_1)
);

halfband_filter u_halfband_2 (
    .aclk                (clk),
    .aresetn             (fir_aresetn),
    .s_axis_data_tvalid  (hb_valid_1),
    .s_axis_data_tready  (hb_ready_2),
    .s_axis_data_tdata   (hb_data_1),
    .m_axis_data_tvalid  (hb_raw_valid_2),
    .m_axis_data_tdata   (hb_raw_2)
);

halfband_filter u_halfband_3 (
    .aclk                (clk),
    .aresetn             (fir_aresetn),
    .s_axis_data_tvalid  (hb_valid_2),
    .s_axis_data_tready  (hb_ready_3),
    .s_axis_data_tdata   (hb_data_2),
    .m_axis_data_tvalid  (hb_raw_valid_3),
    .m_axis_data_tdata   (hb_raw_3)
);

halfband_filter u_halfband_4 (
    .aclk                (clk),
    .aresetn             (fir_aresetn),
    .s_axis_data_tvalid  (hb_valid_3),
    .s_axis_data_tready  (hb_ready_4),
    .s_axis_data_tdata   (hb_data_3),
    .m_axis_data_tvalid  (hb_raw_valid_4),
    .m_axis_data_tdata   (hb_raw_4)
);

// ----------------------------------------------------------------------------
// 选择送入末级FIR的数据
//
// level=1：输入数据直接进入末级；
// level=2～6：分别选择前面第1～5级的输出进入末级。
// 因此末级始终作为启用HB时的最后一级，总抽取倍率为2^hb_level。
// ----------------------------------------------------------------------------
reg signed [31:0] last_input_data;
reg               last_input_valid;

always @(*) begin
    last_input_data  = data_in;
    last_input_valid = 1'b0;

    case (active_hb_level)
        3'd1: begin
            last_input_data  = data_in;
            last_input_valid = data_in_valid && fir_aresetn;
        end
        3'd2: begin
            last_input_data  = hb_data_0;
            last_input_valid = hb_valid_0;
        end
        3'd3: begin
            last_input_data  = hb_data_1;
            last_input_valid = hb_valid_1;
        end
        3'd4: begin
            last_input_data  = hb_data_2;
            last_input_valid = hb_valid_2;
        end
        3'd5: begin
            last_input_data  = hb_data_3;
            last_input_valid = hb_valid_3;
        end
        3'd6: begin
            last_input_data  = hb_data_4;
            last_input_valid = hb_valid_4;
        end
        default: begin
            last_input_data  = data_in;
            last_input_valid = 1'b0;
        end
    endcase
end

// ----------------------------------------------------------------------------
// 末级FIR及其舍入截位
//
// 当前last_hb.coe的有符号系数和为65532，接近2^16，因此系数采用Q16尺度。
// 为恢复到与输入一致的32位幅度，末级结果必须舍入后右移16位。
// 这与普通halfband_filter的Q15系数不同，不能继续沿用[46:15]。
// ----------------------------------------------------------------------------
wire        [55:0] last_raw_axis;
wire signed [48:0] last_raw_data = $signed(last_raw_axis[48:0]);
wire signed [48:0] last_rounded  = last_raw_data + 49'sd32767;
wire               last_raw_valid;
wire               last_ready;

reg signed [31:0] last_data_out;
reg               last_data_valid;

last_hb u_last_hb (
    .aclk                (clk),
    .aresetn             (fir_aresetn),
    .s_axis_data_tvalid  (last_input_valid),
    .s_axis_data_tready  (last_ready),
    .s_axis_data_tdata   (last_input_data),
    .m_axis_data_tvalid  (last_raw_valid),
    .m_axis_data_tdata   (last_raw_axis)
);

always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
        last_data_out   <= 32'sd0;
        last_data_valid <= 1'b0;
    end
    else if (!fir_aresetn) begin
        last_data_out   <= 32'sd0;
        last_data_valid <= 1'b0;
    end
    else begin
        last_data_valid <= last_raw_valid;
        if (last_raw_valid)
            last_data_out <= last_rounded[47:16];
    end
end

// level=0时完全旁路HB；其余档位输出末级FIR结果。
assign data_out = (active_hb_level == 3'd0) ? data_in : last_data_out;
assign data_out_valid = (active_hb_level == 3'd0) ?
                        (data_in_valid && fir_aresetn) :
                        last_data_valid;

// 当前两个FIR IP都按每拍可接收一个样点配置，正常运行时ready应保持为1。
// ready信号保留为内部观测点，后续可加入ILA检查是否出现意外反压。

endmodule
