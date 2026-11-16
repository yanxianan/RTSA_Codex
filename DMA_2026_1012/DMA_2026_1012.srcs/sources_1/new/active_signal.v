`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////////
// Company: 
// Engineer: 
// 
// Create Date: 2024/07/29 17:43:41
// Design Name: 
// Module Name: active_signal
// Project Name: 
// Target Devices: 
// Tool Versions: 
// Description: 
// 
// Dependencies: 
// 
// Revision:
// Revision 0.01 - File Created
// Additional Comments:
// 
//////////////////////////////////////////////////////////////////////////////////


module active_signal#(
    parameter LANE = 4,
    parameter BIT  = 16,
    parameter bit_small = 14
)(
    input                              sys_clk         ,
    input                              sys_rst         ,
    input                              dds_en          ,
    input                              resync          ,
    input           [31 : 0]           phase_step      ,

    output     reg  [bit_small-1:0]          dds_signal_i0    ,
    output     reg  [bit_small-1:0]          dds_signal_i1    ,
    output     reg  [bit_small-1:0]          dds_signal_i2    ,
    output     reg  [bit_small-1:0]          dds_signal_i3    ,

    output     reg  [bit_small-1:0]          dds_signal_q0    ,
    output     reg  [bit_small-1:0]          dds_signal_q1    ,
    output     reg  [bit_small-1:0]          dds_signal_q2    ,
    output     reg  [bit_small-1:0]          dds_signal_q3    ,           
    
    output                             dds_signal_valid
    );

//debug_input
reg                      debug_sys_rst               ;
reg                      debug_resync                ;
reg    [31 : 0]          debug_phase_step            ;

reg     [31 : 0]          f_word                      ;
reg     [31 : 0]          p_word          [LANE-1:0];
genvar                    i                           ; 
integer                   j                           ;  
integer                   k                           ;    
wire    [BIT*2-1 : 0]     sin_cos         [LANE-1:0];
reg     [71 : 0]          control_word    [LANE-1:0]; 

reg     [BIT-1 : 0]       sin_reg         [LANE-1:0];
reg     [BIT-1 : 0]       cos_reg         [LANE-1:0];

(* equivalent_reg_removal = "no" , KEEP = "TRUE"  *)
reg	    rst_r1 = 1;
    (* max_fanout=50 *)
reg     [LANE-1 : 0]    rst_r2={LANE{1'b1}};
reg     [LANE-1 : 0]    rst_r3={LANE{1'b1}};
reg     [LANE-1 : 0]    rst_r4={LANE{1'b1}};
always @ (posedge sys_clk)
    rst_r1 <= sys_rst;
always @ (posedge sys_clk)
    rst_r2 <= {LANE{rst_r1}};
always @ (posedge sys_clk)
    rst_r3 <= rst_r2;
always @ (posedge sys_clk)
    rst_r4 <= rst_r3;

always @ (posedge sys_clk)begin
    if(sys_rst)begin
        f_word <= 32'd0;
        for(j=0; j<LANE; j=j+1)begin
            p_word[j] <= 32'd0;
        end
    end
    else begin
        p_word[0 ] <= 32'd0 ;
        for(j=1; j<LANE; j=j+1)begin
            p_word[j] <= p_word[j-1] + phase_step;
        end
        f_word <= p_word[LANE-1] +  phase_step;
    end
end
wire dds_valid[LANE-1:0];

generate
    for(i=0;i<LANE;i=i+1)
    begin: dds_test
        always @(posedge sys_clk )
            control_word[i] <= {7'd0,resync,p_word[i],f_word};
        dds_0 u_dds_32 (
        .aclk(sys_clk),                                // input wire aclk
        .aresetn(~rst_r4[i]),                          // input wire aresetn
        .s_axis_phase_tvalid(1'b1),  // input wire s_axis_phase_tvalid
        .s_axis_phase_tdata(control_word[i]),    // input wire [71 : 0] s_axis_phase_tdata
        .m_axis_data_tvalid(dds_valid[i]),    // output wire m_axis_data_tvalid
        .m_axis_data_tdata(sin_cos[i])      // output wire [15 : 0] m_axis_data_tdata
        );
        always @(posedge sys_clk )
            cos_reg[i] <= sin_cos[i][15 :  0];
        always @(posedge sys_clk )
            sin_reg[i] <= sin_cos[i][31 : 16];
    end
endgenerate

// always @(posedge sys_clk ) begin
//     if(sys_rst)
//         active_dds_data <= 'd0;
//     else begin
//         for(k=0; k<LANE; k=k+1)
//         begin
//             active_dds_data[k*16+:16] <= sin_cos[k];
//         end
//     end
// end



always @ (posedge sys_clk)begin
    if(dds_en)begin
        dds_signal_i0  <= cos_reg[0 ][13:0];
        dds_signal_i1  <= cos_reg[1 ][13:0];
        dds_signal_i2  <= cos_reg[2 ][13:0];
        dds_signal_i3  <= cos_reg[3 ][13:0];
        dds_signal_q0  <= sin_reg[0 ][13:0];
        dds_signal_q1  <= sin_reg[1 ][13:0];
        dds_signal_q2  <= sin_reg[2 ][13:0];
        dds_signal_q3  <= sin_reg[3 ][13:0];
    end else begin
        dds_signal_i0  <= 0;
        dds_signal_i1  <= 0;
        dds_signal_i2  <= 0;
        dds_signal_i3  <= 0;
        dds_signal_q0  <= 0;
        dds_signal_q1  <= 0;
        dds_signal_q2  <= 0;
        dds_signal_q3  <= 0;
    end

end
assign dds_signal_valid = dds_valid[0] && dds_valid[1] && dds_valid[2] && dds_valid[3];

// always @(posedge sys_clk ) begin
//     debug_sys_rst    <= sys_rst   ;
//     debug_resync     <= resync    ;
//     debug_phase_step <= phase_step;    
// end

// ila_active_signal u_ila_active_signal (
// 	.clk(sys_clk), // input wire clk


// 	.probe0(sin_reg[0 ]      ), // input wire [15:0] probe0
//     .probe1(debug_sys_rst    ), // input wire [0:0]  probe1 
// 	.probe2(debug_resync     ), // input wire [0:0]  probe2 
// 	.probe3(debug_phase_step ), // input wire [31:0]  probe3 
// 	.probe4(p_word[1]       ), // input wire [31:0]  probe4 
// 	.probe5(f_word           )  // input wire [31:0]  probe5 
// );

endmodule
