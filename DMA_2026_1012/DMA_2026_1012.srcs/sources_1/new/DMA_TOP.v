`timescale 1ns / 1ps

// =============================================================================
// 模块名称：DMA_TOP
// 功能说明：工程板级顶层。
//
// 本模块只负责：
// 1. 连接板级模拟输入、采样时钟、SYSREF和PL参考时钟；
// 2. 例化Block Design封装AXI_DMA_wrapper；
// 3. 例化logic_top，并连接RFADC、PS寄存器接口和DMA数据接口。
// =============================================================================

module DMA_TOP (
    // RFDC模拟输入、采样时钟和SYSREF。
    input  wire         sysref_in_0_diff_n,
    input  wire         sysref_in_0_diff_p,
    input  wire         vin0_01_0_v_n,
    input  wire         vin0_01_0_v_p,
    input  wire         vin1_01_0_v_n,
    input  wire         vin1_01_0_v_p,
    input  wire         adc1_clk_0_clk_n,
    input  wire         adc1_clk_0_clk_p,

    // PL参考时钟和外部MTS SYSREF时钟。
    input  wire [0:0]   CLK_DIFF_1_PL_CLK_clk_n,
    input  wire [0:0]   CLK_DIFF_1_PL_CLK_clk_p,
    input  wire [0:0]   CLK_DIFF_2_SYSREF_clk_n,
    input  wire [0:0]   CLK_DIFF_2_SYSREF_clk_p,

    // 保留的通用寄存器访问接口。
    output wire         reg_clk,
    output wire [11:0]  reg_addr,
    output wire         reg_wr_en,
    output wire         reg_rd_en,
    output wire [15:0]  reg_data
);

// -----------------------------------------------------------------------------
// PS通过AXI BRAM访问PL寄存器的接口。
// -----------------------------------------------------------------------------
wire [12:0] axi_bram_addr;
wire        axi_bram_clk;
wire [31:0] axi_bram_wd_din;
wire [31:0] axi_bram_rd_dout;
wire        axi_bram_en;
wire        axi_bram_rst;
wire [3:0]  axi_bram_we;

// -----------------------------------------------------------------------------
// Block Design产生的数据通路时钟和低有效复位。
// -----------------------------------------------------------------------------
wire       r_clk;
wire       w_clk;
wire [0:0] r_rst;
wire [0:0] w_rst;

// -----------------------------------------------------------------------------
// RFDC输出的四路AXI4-Stream数据。
// Tile 0：m00为I、m01为Q；Tile 1：m10为I、m11为Q。
// -----------------------------------------------------------------------------
wire [63:0] m00_axis_tdata;
wire        m00_axis_tvalid;
wire        m00_axis_tready;
wire [63:0] m01_axis_tdata;
wire        m01_axis_tvalid;
wire        m01_axis_tready;
wire [63:0] m10_axis_tdata;
wire        m10_axis_tvalid;
wire        m10_axis_tready;
wire [63:0] m11_axis_tdata;
wire        m11_axis_tvalid;
wire        m11_axis_tready;

// -----------------------------------------------------------------------------
// logic_top送往Block Design中FIFO/DMA的数据。
// -----------------------------------------------------------------------------
wire [63:0] m_axis_data_tdata;
wire [7:0]  m_axis_data_tkeep;
wire        m_axis_data_tvalid;
wire        m_axis_data_tlast;
wire        m_axis_data_tready;

// 当前不使用的RFADC过量程状态和GPIO输出。
wire        adc0_01_over_range_0;
wire        adc1_01_over_range_0;
wire [0:0]  gpio_io_o_0;

// -----------------------------------------------------------------------------
// Block Design：包含RFDC、PS、AXI BRAM控制器、FIFO和AXI DMA。
// -----------------------------------------------------------------------------
AXI_DMA_wrapper u0_AXI_DMA_wrapper (
    .BRAM_PORTA_0_addr          (axi_bram_addr),
    .BRAM_PORTA_0_clk           (axi_bram_clk),
    .BRAM_PORTA_0_din           (axi_bram_wd_din),
    .BRAM_PORTA_0_dout          (axi_bram_rd_dout),
    .BRAM_PORTA_0_en            (axi_bram_en),
    .BRAM_PORTA_0_rst           (axi_bram_rst),
    .BRAM_PORTA_0_we            (axi_bram_we),

    .CLK_DIFF_1_PL_CLK_clk_n    (CLK_DIFF_1_PL_CLK_clk_n),
    .CLK_DIFF_1_PL_CLK_clk_p    (CLK_DIFF_1_PL_CLK_clk_p),
    .CLK_DIFF_2_SYSREF_clk_n    (CLK_DIFF_2_SYSREF_clk_n),
    .CLK_DIFF_2_SYSREF_clk_p    (CLK_DIFF_2_SYSREF_clk_p),
    .adc1_clk_0_clk_n           (adc1_clk_0_clk_n),
    .adc1_clk_0_clk_p           (adc1_clk_0_clk_p),
    .sysref_in_0_diff_n         (sysref_in_0_diff_n),
    .sysref_in_0_diff_p         (sysref_in_0_diff_p),
    .vin0_01_0_v_n             (vin0_01_0_v_n),
    .vin0_01_0_v_p             (vin0_01_0_v_p),
    .vin1_01_0_v_n             (vin1_01_0_v_n),
    .vin1_01_0_v_p             (vin1_01_0_v_p),

    // 当前软件不使用RFADC过量程清除功能，因此固定为低电平。
    .adc0_01_clear_ov_0        (1'b0),
    .adc0_01_over_range_0      (adc0_01_over_range_0),
    .adc1_01_clear_ov_0        (1'b0),
    .adc1_01_over_range_0      (adc1_01_over_range_0),
    .gpio_io_o_0               (gpio_io_o_0),

    .m00_axis_tdata_0          (m00_axis_tdata),
    .m00_axis_tvalid_0         (m00_axis_tvalid),
    .m00_axis_tready_0         (m00_axis_tready),
    .m01_axis_tdata_0          (m01_axis_tdata),
    .m01_axis_tvalid_0         (m01_axis_tvalid),
    .m01_axis_tready_0         (m01_axis_tready),
    .m10_axis_tdata_0          (m10_axis_tdata),
    .m10_axis_tvalid_0         (m10_axis_tvalid),
    .m10_axis_tready_0         (m10_axis_tready),
    .m11_axis_tdata_0          (m11_axis_tdata),
    .m11_axis_tvalid_0         (m11_axis_tvalid),
    .m11_axis_tready_0         (m11_axis_tready),

    .r_clk                     (r_clk),
    .r_rst                     (r_rst),
    .w_clk                     (w_clk),
    .w_rst                     (w_rst),

    .s_axis_tdata_0            (m_axis_data_tdata),
    .s_axis_tkeep_0            (m_axis_data_tkeep),
    .s_axis_tlast_0            (m_axis_data_tlast),
    .s_axis_tready_0           (m_axis_data_tready),
    .s_axis_tvalid_0           (m_axis_data_tvalid)
);

// -----------------------------------------------------------------------------
// FPGA业务逻辑：寄存器控制、频谱处理、触发捕获和DMA输出选择。
// -----------------------------------------------------------------------------
logic_top u0_logic_top (
    .logic_clk                 (r_clk),
    .r_rst                     (r_rst[0]),
    .w_clk                     (w_clk),
    .w_rst                     (w_rst[0]),

    .axi_bram_addr             (axi_bram_addr),
    .axi_bram_clk              (axi_bram_clk),
    .axi_bram_wd_din           (axi_bram_wd_din),
    .axi_bram_rd_dout          (axi_bram_rd_dout),
    .axi_bram_en               (axi_bram_en),
    .axi_bram_rst              (axi_bram_rst),
    .axi_bram_we               (axi_bram_we),

    .m00_axis_tdata            (m00_axis_tdata),
    .m00_axis_tvalid           (m00_axis_tvalid),
    .m00_axis_tready           (m00_axis_tready),
    .m01_axis_tdata            (m01_axis_tdata),
    .m01_axis_tvalid           (m01_axis_tvalid),
    .m01_axis_tready           (m01_axis_tready),
    .m10_axis_tdata            (m10_axis_tdata),
    .m10_axis_tvalid           (m10_axis_tvalid),
    .m10_axis_tready           (m10_axis_tready),
    .m11_axis_tdata            (m11_axis_tdata),
    .m11_axis_tvalid           (m11_axis_tvalid),
    .m11_axis_tready           (m11_axis_tready),

    .m_axis_data_tready        (m_axis_data_tready),
    .m_axis_data_tdata         (m_axis_data_tdata),
    .m_axis_data_tkeep         (m_axis_data_tkeep),
    .m_axis_data_tvalid        (m_axis_data_tvalid),
    .m_axis_data_tlast         (m_axis_data_tlast),

    .reg_clk                   (reg_clk),
    .reg_addr                  (reg_addr),
    .reg_wr_en                 (reg_wr_en),
    .reg_rd_en                 (reg_rd_en),
    .reg_data                  (reg_data)
);

endmodule
