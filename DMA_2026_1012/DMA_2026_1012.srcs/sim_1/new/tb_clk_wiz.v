`timescale 1ns / 1ps
module tb_clk_wiz();
reg     clk     ;
wire clk_out;
  
initial begin
    clk = 1'b0;
end

always #5 clk = ~clk;

clk_wiz u1_clk_wiz(
    
    .clk_in        (clk  ),
    .clk_out       (clk_out)
);
endmodule

