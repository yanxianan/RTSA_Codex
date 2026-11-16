

module DDS_create(
    input clk,
    input rst_n,
    
    output reg signed [13:0] ch0_data_i,         // 通道0 14bit数据
    output reg signed [13:0] ch1_data_i,         // 通道1 14bit数据
    output reg signed [13:0] ch2_data_i,         // 通道2 14bit数据
    output reg signed [13:0] ch3_data_i,         // 通道3 14bit数据

    output reg signed [13:0] ch0_data_q,         // 通道0 14bit数据
    output reg signed [13:0] ch1_data_q,         // 通道1 14bit数据
    output reg signed [13:0] ch2_data_q,         // 通道2 14bit数据
    output reg signed [13:0] ch3_data_q,         // 通道3 14bit数据
    
    output reg               data_out_valid    // 单bit总有效标志
);

wire dds0_vld;
wire signed [15:0] dds0_tdata_i;
wire signed [15:0] dds0_tdata_q;

wire dds1_vld;
wire signed [15:0] dds1_tdata_i;
wire signed [15:0] dds1_tdata_q;

wire dds2_vld;
wire signed [15:0] dds2_tdata_i;
wire signed [15:0] dds2_tdata_q;

wire dds3_vld;
wire signed [15:0] dds3_tdata_i;
wire signed [15:0] dds3_tdata_q;

dds_compiler_0 u0_dds_compiler_0
(
    .aclk(clk),
    .aresetn(rst_n),
    .m_axis_data_tvalid(dds0_vld),
    .m_axis_data_tdata({dds0_tdata_q,dds0_tdata_i})
);

dds_compiler_1 u1_dds_compiler_1
(
    .aclk(clk),
    .aresetn(rst_n),
    .m_axis_data_tvalid(dds1_vld),
    .m_axis_data_tdata({dds1_tdata_q,dds1_tdata_i})
);

dds_compiler_2 u2_dds_compiler_2
(
    .aclk(clk),
    .aresetn(rst_n),
    .m_axis_data_tvalid(dds2_vld),
    .m_axis_data_tdata({dds2_tdata_q,dds2_tdata_i})
);

dds_compiler_3 u3_dds_compiler_3
(
    .aclk(clk),
    .aresetn(rst_n),
    .m_axis_data_tvalid(dds3_vld),
    .m_axis_data_tdata({dds3_tdata_q,dds3_tdata_i})
);

// 1. 四路valid做或运算，合并成1位总有效信号
always @(*) begin
    data_out_valid = dds0_vld || dds1_vld || dds2_vld || dds3_vld;
end

// 2. DDS 16bit波形截取低14bit，时序寄存输出
always @(posedge clk or negedge rst_n) begin
    if(!rst_n) begin
        ch0_data_i<= 14'd0;
        ch1_data_i<= 14'd0;
        ch2_data_i<= 14'd0;
        ch3_data_i<= 14'd0;
        ch0_data_q<= 14'd0;
        ch1_data_q<= 14'd0;
        ch2_data_q<= 14'd0;
        ch3_data_q<= 14'd0;
    end else begin
        ch0_data_i <= dds0_tdata_i[13:0];
        ch1_data_i <= dds1_tdata_i[13:0];
        ch2_data_i <= dds2_tdata_i[13:0];
        ch3_data_i <= dds3_tdata_i[13:0];

        ch0_data_q <= dds0_tdata_q[13:0];
        ch1_data_q <= dds1_tdata_q[13:0];
        ch2_data_q <= dds2_tdata_q[13:0];
        ch3_data_q <= dds3_tdata_q[13:0];
    end
end

endmodule