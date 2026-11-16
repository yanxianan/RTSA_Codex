`timescale 1ns / 1ps

// ============================================================================
// 1024点四项Blackman-Harris加窗模块
//
// 数据接口约定：
//   1. data_in_valid为1时，data_in表示一个需要接收的复数时域采样点。
//   2. 每帧包含1024个有效采样点。
//   3. 本模块没有逐点ready接口；上游应在整帧开始前确认下游能够接收。
//   4. 流水线填满后，本模块可以做到每个时钟接收并输出一个复数采样点。
//
// 复数数据格式：
//   data_in [31:0]  ：32位有符号I路/实部数据
//   data_in [63:32] ：32位有符号Q路/虚部数据
//   data_out采用相同的数据排列方式。
//
// 窗系数格式：
//   rom_blackman_harris_1024.douta为18位无符号Q1.17系数。
//   I路和Q路使用同一个实数窗系数。
//   乘法器输出完整50位乘积，随后进行对称四舍五入并除以2^17，
//   使输出数据的小数点位置与输入保持一致。
//
// 流水线结构：
//   第1级：接收输入采样点，同时向ROM发出窗系数地址
//   第2级：将输入采样点与ROM两拍延迟后的窗系数对齐
//   第3～6级：两个Multiplier Generator IP内部的4级乘法流水线
//   第7级：对乘积进行对称四舍五入、缩放并寄存输出
// ============================================================================

module window_rom (
    input  wire         clk,
    input  wire         rst,

    input  wire [63:0]  data_in,
    input  wire         data_in_valid,

    output reg  [63:0]  data_out,
    output reg          data_out_valid,
    output reg          data_out_last
);

localparam [9:0] LAST_SAMPLE_INDEX = 10'd1023;

// --------------------------------------------------------------------------
// 窗系数地址生成
//
// window_addr始终表示当前正在接收的采样点所对应的窗系数地址。
// 只有data_in_valid为1时地址才会递增，因此输入有效信号中出现空拍时，
// 采样点序号与窗系数序号不会发生错位。
// --------------------------------------------------------------------------
reg [9:0] window_addr;

always @(posedge clk) begin
    if (rst) begin
        window_addr <= 10'd0;
    end
    else if (data_in_valid) begin
        if (window_addr == LAST_SAMPLE_INDEX)
            window_addr <= 10'd0;
        else
            window_addr <= window_addr + 1'b1;
    end
end

// --------------------------------------------------------------------------
// Blackman-Harris窗系数ROM
//
// 当前Block Memory Generator配置的总读取延迟为2拍：
//   第1拍为同步存储器读取；
//   第2拍为BRAM原语输出寄存器。
//
// ena固定为1，使ROM内部两级流水线在每个时钟都正常前进。
// 当data_in_valid为0时，地址保持不变，ROM会重复读出当前地址；
// 但对应的valid流水线为0，因此重复系数不会被当作有效结果使用。
// --------------------------------------------------------------------------
wire [17:0] window_coeff;

rom_blackman_harris_1024 u_window_coeff_rom (
    .clka  (clk),
    .ena   (1'b1),
    .addra (window_addr),
    .douta (window_coeff)
);

// --------------------------------------------------------------------------
// 第1～2级：按照ROM的2拍读取延迟，对齐数据和控制信息
//
// sample_index_s1和sample_index_s2仅用于仿真、ILA观察和时序核对，
// 不参与实际乘法运算。
// --------------------------------------------------------------------------
reg [63:0] data_s1;
reg [63:0] data_s2;
reg        valid_s1;
reg        valid_s2;
reg        last_s1;
reg        last_s2;
reg [9:0]  sample_index_s1;
reg [9:0]  sample_index_s2;

always @(posedge clk) begin
    if (rst) begin
        data_s1          <= 64'd0;
        data_s2          <= 64'd0;
        valid_s1         <= 1'b0;
        valid_s2         <= 1'b0;
        last_s1          <= 1'b0;
        last_s2          <= 1'b0;
        sample_index_s1  <= 10'd0;
        sample_index_s2  <= 10'd0;
    end
    else begin
        valid_s1 <= data_in_valid;
        valid_s2 <= valid_s1;

        // last只允许在有效采样点上产生，防止空拍中误报帧结束。
        last_s1 <= data_in_valid &&
                   (window_addr == LAST_SAMPLE_INDEX);
        last_s2 <= last_s1;

        if (data_in_valid) begin
            data_s1         <= data_in;
            sample_index_s1 <= window_addr;
        end

        if (valid_s1) begin
            data_s2         <= data_s1;
            sample_index_s2 <= sample_index_s1;
        end
    end
end

// --------------------------------------------------------------------------
// 第3～6级：I/Q两路并行乘以同一个18位无符号Q1.17窗系数
//
// mult_window_32s_18u的实际配置为：
//   A：32位有符号数
//   B：18位无符号数
//   P：50位完整精度乘积
//   内部流水线：4拍
//   CE、SCLR：未启用
//
// 一个IP定义在这里实例化两次，综合后形成I路和Q路两套并行乘法硬件。
// --------------------------------------------------------------------------
wire signed [31:0] data_i_s2;
wire signed [31:0] data_q_s2;
wire signed [49:0] product_i_mult;
wire signed [49:0] product_q_mult;

assign data_i_s2 = data_s2[31:0];
assign data_q_s2 = data_s2[63:32];

mult_window_32s_18u u_mult_window_i (
    .CLK (clk),
    .A   (data_i_s2),
    .B   (window_coeff),
    .P   (product_i_mult)
);

mult_window_32s_18u u_mult_window_q (
    .CLK (clk),
    .A   (data_q_s2),
    .B   (window_coeff),
    .P   (product_q_mult)
);

// --------------------------------------------------------------------------
// 乘法器控制信息对齐
//
// 乘法器IP没有valid、last和样本序号端口，因此这些控制信息必须在外部
// 同样延迟4拍。mult_valid_pipe[3]为1时，product_i_mult和
// product_q_mult才表示一个有效乘法结果。
// --------------------------------------------------------------------------
reg [3:0] mult_valid_pipe;
reg [3:0] mult_last_pipe;
reg [9:0] sample_index_mult_s1;
reg [9:0] sample_index_mult_s2;
reg [9:0] sample_index_mult_s3;
reg [9:0] sample_index_mult_s4;

always @(posedge clk) begin
    if (rst) begin
        mult_valid_pipe       <= 4'b0000;
        mult_last_pipe        <= 4'b0000;
        sample_index_mult_s1  <= 10'd0;
        sample_index_mult_s2  <= 10'd0;
        sample_index_mult_s3  <= 10'd0;
        sample_index_mult_s4  <= 10'd0;
    end
    else begin
        mult_valid_pipe <= {mult_valid_pipe[2:0], valid_s2};
        mult_last_pipe  <= {mult_last_pipe[2:0],  last_s2};

        // 样本序号与乘法器内部4级流水线逐拍同步移动。
        sample_index_mult_s1 <= sample_index_s2;
        sample_index_mult_s2 <= sample_index_mult_s1;
        sample_index_mult_s3 <= sample_index_mult_s2;
        sample_index_mult_s4 <= sample_index_mult_s3;
    end
end

// --------------------------------------------------------------------------
// 对Q1.17乘法结果进行有符号对称四舍五入
//
// 正数： (乘积 + 2^16) >>> 17
// 负数：-((|乘积| + 2^16) >>> 17)
//
// 窗系数不大于1，因此合法的32位有符号输入经过加窗后不会放大，
// 缩放后的结果仍可安全放入32位有符号输出。
// --------------------------------------------------------------------------
function automatic signed [31:0] round_q17_to_s32;
    input signed [49:0] product;
    reg signed [50:0] product_ext;
    reg signed [50:0] magnitude;
    reg signed [50:0] rounded_value;
    begin
        product_ext = {product[49], product};

        if (product_ext >= 0) begin
            rounded_value = (product_ext + 51'sd65536) >>> 17;
        end
        else begin
            magnitude     = -product_ext;
            rounded_value = -((magnitude + 51'sd65536) >>> 17);
        end

        round_q17_to_s32 = rounded_value[31:0];
    end
endfunction

wire signed [31:0] rounded_i;
wire signed [31:0] rounded_q;

assign rounded_i = round_q17_to_s32(product_i_mult);
assign rounded_q = round_q17_to_s32(product_q_mult);

// --------------------------------------------------------------------------
// 第7级：寄存缩放后的输出数据及其控制信息
//
// sample_index_out保留在模块内部，便于仿真或ILA确认每个输出点对应的
// 窗系数地址；它不参与外部接口功能。
// --------------------------------------------------------------------------
reg [9:0] sample_index_out;

always @(posedge clk) begin
    if (rst) begin
        data_out         <= 64'd0;
        data_out_valid   <= 1'b0;
        data_out_last    <= 1'b0;
        sample_index_out <= 10'd0;
    end
    else begin
        data_out_valid <= mult_valid_pipe[3];
        data_out_last  <= mult_valid_pipe[3] && mult_last_pipe[3];

        if (mult_valid_pipe[3]) begin
            data_out         <= {rounded_q, rounded_i};
            sample_index_out <= sample_index_mult_s4;
        end
    end
end

endmodule
