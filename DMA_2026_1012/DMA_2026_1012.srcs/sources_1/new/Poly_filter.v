`timescale 1ns / 1ps
module Poly_filter(
    input clk,
    input rst_n,
    input signed [13:0] ch0_data,
    input signed [13:0] ch1_data,
    input signed [13:0] ch2_data,
    input signed [13:0] ch3_data,
    input data_in_valid,
    
    output signed [15:0] data_out,
    output data_out_valid  
    );
    
wire [31:0] data_out_0;
wire data_out_valid_0;
Poly_0 U0
(
    .aclk(clk),
    .aresetn(rst_n),
    .s_axis_data_tvalid(data_in_valid),
    .s_axis_data_tready(),
    .s_axis_data_tdata({{50{ch3_data[13]}}, ch3_data}),
    .m_axis_data_tvalid(data_out_valid_0),
    .m_axis_data_tdata(data_out_0)
 );

wire [31:0] data_out_1;
wire data_out_valid_1;
Poly_1 U1
(
    .aclk(clk),
    .aresetn(rst_n),
    .s_axis_data_tvalid(data_in_valid),
    .s_axis_data_tready(),
    .s_axis_data_tdata({{50{ch2_data[13]}}, ch2_data}),
    .m_axis_data_tvalid(data_out_valid_1),
    .m_axis_data_tdata(data_out_1)
 );
 
wire [31:0] data_out_2;
wire data_out_valid_2;
Poly_2 U2
(
    .aclk(clk),
    .aresetn(rst_n),
    .s_axis_data_tvalid(data_in_valid),
    .s_axis_data_tready(),
    .s_axis_data_tdata({{50{ch1_data[13]}}, ch1_data}),
    .m_axis_data_tvalid(data_out_valid_2),
    .m_axis_data_tdata(data_out_2)
 );
 
wire [31:0] data_out_3;
wire data_out_valid_3;
Poly_3 U3
(
    .aclk(clk),
    .aresetn(rst_n),
    .s_axis_data_tvalid(data_in_valid),
    .s_axis_data_tready(),
    .s_axis_data_tdata({{50{ch0_data[13]}}, ch0_data}),
    .m_axis_data_tvalid(data_out_valid_3),
    .m_axis_data_tdata(data_out_3)
 ); 
wire [17:0] data_out_18;
assign data_out_18= data_out_0[28:13] + data_out_1[28:13] + data_out_2[28:13] + data_out_3[28:13];
assign data_out = data_out_18[15:0];
assign data_out_valid = data_out_valid_0 & data_out_valid_1 &data_out_valid_2 & data_out_valid_3;
endmodule
