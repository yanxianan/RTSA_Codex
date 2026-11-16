`timescale 1ns / 1ps

//==============================================================================
// 频率触发通路
//
// 功能：
// 1. 观察最大保持之前的原始FFT数据，在指定频段内比较线性功率；
// 2. 频点功率超过门限时产生单周期触发脉冲；
// 3. 使用该触发脉冲保存触发前、触发后的抽取时域IQ数据；
// 4. 捕获结束后通过AXI4-Stream输出时域IQ数据。
//
// 数据来源：
// fft_*     来自overlap_fft_module的原始FFT输出；
// time_data 来自dec_module的抽取后时域IQ输出。
//==============================================================================
module trigger_module (
    input  wire         clk,
    input  wire         rst,

    // 最大保持之前的原始FFT流。
    input  wire [63:0]  fft_tdata,
    input  wire         fft_tvalid,
    input  wire         fft_tready,
    input  wire         fft_tlast,

    // 抽取后的时域IQ数据，[31:0]为I，[63:32]为Q。
    input  wire [63:0]  time_data,
    input  wire         time_data_valid,

    // 频率触发配置。
    input  wire         trigger_enable,
    input  wire         trigger_clear,
    input  wire [9:0]   start_bin,
    input  wire [9:0]   end_bin,
    input  wire [63:0]  power_threshold,

    // 触发结果。
    output wire         trigger_pulse,
    output wire         trigger_latched,
    output wire [9:0]   trigger_bin,
    output wire [63:0]  trigger_power,
    output wire [63:0]  trigger_threshold,

    // 时域捕获配置，点数单位为有效IQ采样点。
    input  wire         capture_arm,
    input  wire         capture_clear,
    input  wire [31:0]  pre_sample_num,
    input  wire [31:0]  post_sample_num,

    // 捕获后的时域IQ AXI4-Stream输出。
    output wire [63:0]  m_axis_tdata,
    output wire [7:0]   m_axis_tkeep,
    output wire         m_axis_tvalid,
    input  wire         m_axis_tready,
    output wire         m_axis_tlast,

    // 捕获状态。
    output wire         capture_busy,
    output wire         capture_ready,
    output wire         capture_triggered,
    output wire         capture_done,
    output wire [31:0]  captured_sample_num
);

//------------------------------------------------------------------------------
// 频率模板触发
//------------------------------------------------------------------------------
// capture_ready为1表示触发前历史数据已经达到配置长度。
// 只有此时才允许频率模块响应门限越界，避免早期触发被捕获模块忽略后，
// trigger_latched又阻止后续有效触发。
wire trigger_compare_enable = trigger_enable && capture_ready;

frequency_mask_trigger u_frequency_mask_trigger (
    .clk               (clk),
    .rst               (rst),

    .fft_tdata         (fft_tdata),
    .fft_tvalid        (fft_tvalid),
    .fft_tready        (fft_tready),
    .fft_tlast         (fft_tlast),

    .trigger_enable    (trigger_compare_enable),
    .trigger_clear     (trigger_clear),
    .start_bin         (start_bin),
    .end_bin           (end_bin),
    .power_threshold   (power_threshold),

    .trigger_pulse     (trigger_pulse),
    .trigger_latched   (trigger_latched),
    .trigger_bin       (trigger_bin),
    .trigger_power     (trigger_power),
    .trigger_threshold (trigger_threshold)
);

//------------------------------------------------------------------------------
// 触发前后时域IQ捕获
//------------------------------------------------------------------------------
trigger_capture u_trigger_capture (
    .clk                 (clk),
    .rst                 (rst),

    .data_in             (time_data),
    .data_in_valid       (time_data_valid),
    .trigger_in          (trigger_pulse),

    .capture_arm         (capture_arm),
    .capture_clear       (capture_clear),
    .pre_sample_num      (pre_sample_num),
    .post_sample_num     (post_sample_num),

    .m_axis_tdata        (m_axis_tdata),
    .m_axis_tkeep        (m_axis_tkeep),
    .m_axis_tvalid       (m_axis_tvalid),
    .m_axis_tready       (m_axis_tready),
    .m_axis_tlast        (m_axis_tlast),

    .capture_busy        (capture_busy),
    .capture_ready       (capture_ready),
    .capture_triggered   (capture_triggered),
    .capture_done        (capture_done),
    .captured_sample_num (captured_sample_num)
);

endmodule
