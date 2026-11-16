`timescale 1ns / 1ps

// 抽取模块
//
// 功能：
// 1. 在内部DDS、RFADC Tile 0和RFADC Tile 1之间选择输入源；
// 2. 将RFADC每拍携带的4个16位数据恢复为4个14位有符号采样点；
// 3. 分别对I、Q两路执行CIC和HB抽取滤波；
// 4. 输出一路64位时域IQ数据，其中[31:0]为I，[63:32]为Q。
module dec_module (
    input  wire         clk,

    // rst_n只控制RFADC接口是否接收数据；
    // datapath_rst_n还包含数据源切换时产生的内部复位。
    input  wire         rst_n,
    input  wire         datapath_rst_n,

    // 数据源及DDS配置：0空闲，1内部DDS，2 ADC0，3 ADC1。
    input  wire [3:0]   dma_mode,
    input  wire         dds_resync,
    input  wire [15:0]  dds_phase_step_h,
    input  wire [15:0]  dds_phase_step_l,

    // CIC和HB抽取配置，I/Q两路共用同一组配置。
    input  wire [15:0]  cic_rate_cfg,
    input  wire [7:0]   cic_trunc_cfg,
    input  wire [2:0]   hb_level,
    input  wire         cic_cfg_valid,
    output wire         cic_cfg_busy,

    // RFADC Tile 0：m00为I，m01为Q。
    input  wire [63:0]  m00_axis_tdata,
    input  wire         m00_axis_tvalid,
    output wire         m00_axis_tready,
    input  wire [63:0]  m01_axis_tdata,
    input  wire         m01_axis_tvalid,
    output wire         m01_axis_tready,

    // RFADC Tile 1：m10为I，m11为Q。
    input  wire [63:0]  m10_axis_tdata,
    input  wire         m10_axis_tvalid,
    output wire         m10_axis_tready,
    input  wire [63:0]  m11_axis_tdata,
    input  wire         m11_axis_tvalid,
    output wire         m11_axis_tready,

    // 抽取后的时域IQ数据。
    output wire [63:0]  data_out,
    output wire         data_out_valid,

    // 任一ADC Tile的I/Q有效信号没有同时到达时，该信号为1。
    output wire         rfdc_valid_mismatch
);

localparam [3:0] MODE_IDLE = 4'd0;
localparam [3:0] MODE_DDS  = 4'd1;
localparam [3:0] MODE_ADC0 = 4'd2;
localparam [3:0] MODE_ADC1 = 4'd3;

// RFADC输出接口始终被接收。
// 空闲、复位或选择其他数据源时，收到的数据会被直接丢弃。
assign m00_axis_tready = rst_n;
assign m01_axis_tready = rst_n;
assign m10_axis_tready = rst_n;
assign m11_axis_tready = rst_n;

// -------------------------------------------------------------------------
// 内部DDS信号源
// -------------------------------------------------------------------------
wire signed [13:0] dds_i0;
wire signed [13:0] dds_i1;
wire signed [13:0] dds_i2;
wire signed [13:0] dds_i3;
wire signed [13:0] dds_q0;
wire signed [13:0] dds_q1;
wire signed [13:0] dds_q2;
wire signed [13:0] dds_q3;
wire               dds_valid;

active_signal u_active_signal (
    .sys_clk          (clk),
    .sys_rst          (!datapath_rst_n),
    .dds_en           (dma_mode == MODE_DDS),
    .resync           (dds_resync),
    .phase_step       ({dds_phase_step_h, dds_phase_step_l}),
    .dds_signal_i0    (dds_i0),
    .dds_signal_i1    (dds_i1),
    .dds_signal_i2    (dds_i2),
    .dds_signal_i3    (dds_i3),
    .dds_signal_q0    (dds_q0),
    .dds_signal_q1    (dds_q1),
    .dds_signal_q2    (dds_q2),
    .dds_signal_q3    (dds_q3),
    .dds_signal_valid (dds_valid)
);

// -------------------------------------------------------------------------
// RFADC数据拆分
// -------------------------------------------------------------------------
// RFADC每个16位数据的低2位为补零位，[15:2]才是原始14位有符号数据。
wire signed [13:0] adc0_i0 = $signed(m00_axis_tdata[15:2]);
wire signed [13:0] adc0_i1 = $signed(m00_axis_tdata[31:18]);
wire signed [13:0] adc0_i2 = $signed(m00_axis_tdata[47:34]);
wire signed [13:0] adc0_i3 = $signed(m00_axis_tdata[63:50]);
wire signed [13:0] adc0_q0 = $signed(m01_axis_tdata[15:2]);
wire signed [13:0] adc0_q1 = $signed(m01_axis_tdata[31:18]);
wire signed [13:0] adc0_q2 = $signed(m01_axis_tdata[47:34]);
wire signed [13:0] adc0_q3 = $signed(m01_axis_tdata[63:50]);

wire signed [13:0] adc1_i0 = $signed(m10_axis_tdata[15:2]);
wire signed [13:0] adc1_i1 = $signed(m10_axis_tdata[31:18]);
wire signed [13:0] adc1_i2 = $signed(m10_axis_tdata[47:34]);
wire signed [13:0] adc1_i3 = $signed(m10_axis_tdata[63:50]);
wire signed [13:0] adc1_q0 = $signed(m11_axis_tdata[15:2]);
wire signed [13:0] adc1_q1 = $signed(m11_axis_tdata[31:18]);
wire signed [13:0] adc1_q2 = $signed(m11_axis_tdata[47:34]);
wire signed [13:0] adc1_q3 = $signed(m11_axis_tdata[63:50]);

wire adc0_valid = m00_axis_tvalid && m01_axis_tvalid;
wire adc1_valid = m10_axis_tvalid && m11_axis_tvalid;

// 该信号仅用于调试，表示同一个Tile的I/Q有效信号没有对齐。
wire adc0_valid_mismatch = m00_axis_tvalid ^ m01_axis_tvalid;
wire adc1_valid_mismatch = m10_axis_tvalid ^ m11_axis_tvalid;
assign rfdc_valid_mismatch = adc0_valid_mismatch |
                             adc1_valid_mismatch;

// -------------------------------------------------------------------------
// 输入信号源选择
// -------------------------------------------------------------------------
reg signed [13:0] source_i0;
reg signed [13:0] source_i1;
reg signed [13:0] source_i2;
reg signed [13:0] source_i3;
reg signed [13:0] source_q0;
reg signed [13:0] source_q1;
reg signed [13:0] source_q2;
reg signed [13:0] source_q3;
reg               source_valid;

always @(*) begin
    source_i0    = 14'sd0;
    source_i1    = 14'sd0;
    source_i2    = 14'sd0;
    source_i3    = 14'sd0;
    source_q0    = 14'sd0;
    source_q1    = 14'sd0;
    source_q2    = 14'sd0;
    source_q3    = 14'sd0;
    source_valid = 1'b0;

    case (dma_mode)
        MODE_DDS: begin
            source_i0    = dds_i0;
            source_i1    = dds_i1;
            source_i2    = dds_i2;
            source_i3    = dds_i3;
            source_q0    = dds_q0;
            source_q1    = dds_q1;
            source_q2    = dds_q2;
            source_q3    = dds_q3;
            source_valid = dds_valid;
        end

        MODE_ADC0: begin
            source_i0    = adc0_i0;
            source_i1    = adc0_i1;
            source_i2    = adc0_i2;
            source_i3    = adc0_i3;
            source_q0    = adc0_q0;
            source_q1    = adc0_q1;
            source_q2    = adc0_q2;
            source_q3    = adc0_q3;
            source_valid = adc0_valid;
        end

        MODE_ADC1: begin
            source_i0    = adc1_i0;
            source_i1    = adc1_i1;
            source_i2    = adc1_i2;
            source_i3    = adc1_i3;
            source_q0    = adc1_q0;
            source_q1    = adc1_q1;
            source_q2    = adc1_q2;
            source_q3    = adc1_q3;
            source_valid = adc1_valid;
        end

        default: begin
            source_i0    = 14'sd0;
            source_i1    = 14'sd0;
            source_i2    = 14'sd0;
            source_i3    = 14'sd0;
            source_q0    = 14'sd0;
            source_q1    = 14'sd0;
            source_q2    = 14'sd0;
            source_q3    = 14'sd0;
            source_valid = 1'b0;
        end
    endcase
end

// -------------------------------------------------------------------------
// I/Q两路抽取滤波器组
// -------------------------------------------------------------------------
wire [31:0] data_out_i;
wire [31:0] data_out_q;
wire        data_out_valid_i;
wire        data_out_valid_q;
wire        cic_cfg_busy_i;
wire        cic_cfg_busy_q;

// 只有I/Q两路都空闲时才同时接收新配置，保证两路抽取参数一致。
wire cic_cfg_accept = cic_cfg_valid && !cic_cfg_busy;
assign cic_cfg_busy = cic_cfg_busy_i | cic_cfg_busy_q;

dec_filter_bank u_dec_filter_bank_i (
    .clk            (clk),
    .rst_n          (datapath_rst_n),
    .ch0_data       (source_i0),
    .ch1_data       (source_i1),
    .ch2_data       (source_i2),
    .ch3_data       (source_i3),
    .data_in_valid  (source_valid && datapath_rst_n),
    .cic_rate_cfg   (cic_rate_cfg),
    .cic_trunc_cfg  (cic_trunc_cfg),
    .cic_cfg_valid  (cic_cfg_accept),
    .cic_cfg_busy   (cic_cfg_busy_i),
    .hb_level       (hb_level),
    .data_out       (data_out_i),
    .data_out_valid (data_out_valid_i)
);

dec_filter_bank u_dec_filter_bank_q (
    .clk            (clk),
    .rst_n          (datapath_rst_n),
    .ch0_data       (source_q0),
    .ch1_data       (source_q1),
    .ch2_data       (source_q2),
    .ch3_data       (source_q3),
    .data_in_valid  (source_valid && datapath_rst_n),
    .cic_rate_cfg   (cic_rate_cfg),
    .cic_trunc_cfg  (cic_trunc_cfg),
    .cic_cfg_valid  (cic_cfg_accept),
    .cic_cfg_busy   (cic_cfg_busy_q),
    .hb_level       (hb_level),
    .data_out       (data_out_q),
    .data_out_valid (data_out_valid_q)
);

// 只有I/Q两路在同一拍同时有效时，才向后级输出一个完整复数采样点。
assign data_out       = {data_out_q, data_out_i};
assign data_out_valid = data_out_valid_i && data_out_valid_q;

endmodule
