`timescale 1ns / 1ps

// BRAM接口转换：锁存地址和写数据，并产生寄存器访问使能。
module bram_ctrl_intf (
    input  wire        bram_0_clk,
    input  wire [12:0] bram_0_addr,
    input  wire [31:0] bram_0_din,
    input  wire        bram_0_en,
    input  wire [3:0]  bram_0_we,
    output wire        latch_wr_en,
    output wire        latch_rd_en,
    output reg  [12:0] latch_addr,
    output reg  [31:0] latch_wdata
);

reg        wr_en;
reg        rd_en;
reg [11:0] rd_en_dly;

always @(posedge bram_0_clk) begin
    wr_en <= bram_0_en && (bram_0_we == 4'hF);
    rd_en <= bram_0_en && (bram_0_we == 4'h0);
end

// 地址和写数据与wr_en在同一上升沿锁存；下一拍下游读取这组值。
always @(posedge bram_0_clk) begin
    if (bram_0_en)
        latch_addr <= bram_0_addr;
    if (bram_0_en && (bram_0_we == 4'hF))
        latch_wdata <= bram_0_din;
end

// 一次BRAM写入只产生一个时钟周期的写使能，避免事件寄存器重复翻转。
assign latch_wr_en = wr_en;

// 读使能仍沿用原工程的保持方式，不改变现有读回时序。
always @(posedge bram_0_clk)
    rd_en_dly <= {rd_en_dly[10:0], rd_en};

assign latch_rd_en = |rd_en_dly;

ila_bram_ctrl_intf u_ila_bram_ctrl_intf (
    .clk    (bram_0_clk),
    .probe0 (latch_wr_en),
    .probe1 (latch_rd_en),
    .probe2 (latch_addr),
    .probe3 (latch_wdata)
);

endmodule
