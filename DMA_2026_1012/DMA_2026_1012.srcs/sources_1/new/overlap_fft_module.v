`timescale 1ns / 1ps

// 重叠帧FFT模块
//
// 数据通路：
// 抽取后时域IQ -> 重叠组帧 -> Blackman-Harris加窗 -> 1024点FFT
//                -> 多帧最大保持 -> DMA频谱输出
//
// fft_raw_*是最大保持之前的原始FFT流，后续频率模板触发模块
// 应观察这组信号，避免最大保持造成触发时刻延后。
module overlap_fft_module (
    input  wire         clk,
    input  wire         rst_n,

    // 当前数据源有效时为1，空闲模式下禁止生成新帧。
    input  wire         source_enabled,

    // 来自dec_module的抽取后时域IQ数据。
    // data_in[31:0]为I，data_in[63:32]为Q。
    input  wire [63:0]  data_in,
    input  wire         data_in_valid,

    // 最大保持累积的FFT帧数，配置为0时按1帧处理。
    input  wire [15:0]  max_hold_frames_cfg,

    // 相邻帧重叠点数。窄带默认768（75%重叠）；CIC旁路宽带模式使用0，
    // 避免满速时帧控制器读不完一帧而被动丢帧。
    input  wire [15:0]  overlap_ratio_cfg,

    // 最大保持后的频谱输出，送往DMA数据通路。
    input  wire         m_axis_data_tready,
    output wire [63:0]  m_axis_data_tdata,
    output wire         m_axis_data_tvalid,
    output wire         m_axis_data_tlast,

    // 最大保持之前的原始FFT流，仅用于后续触发判断和调试观察。
    output wire [63:0]  fft_raw_tdata,
    output wire         fft_raw_tvalid,
    output wire         fft_raw_tready,
    output wire         fft_raw_tlast,

    // 组帧和FFT缓存状态，供上层ILA观察。
    output wire         frame_drop_pulse,
    output wire [31:0]  frame_drop_count,
    output wire         fft_fifo_overflow_sticky
);

// -------------------------------------------------------------------------
// 1024点重叠组帧
// -------------------------------------------------------------------------
wire [63:0] frame_data;
wire        frame_valid;
wire        fft_frame_can_accept;

frame_ctrl_v3 u_frame_ctrl (
    // 当前版本的写入和读取都工作在同一个clk时钟域。
    .w_rst             (!rst_n),
    .w_clk             (clk),
    .r_rst             (!rst_n),
    .r_clk             (clk),

    // 每帧1024点。窄带768点重叠；宽带直通时由上层配置为0。
    .frame_width_reg   (16'd1024),
    .overlap_ratio_reg (overlap_ratio_cfg),

    .in_valid          (data_in_valid && source_enabled && rst_n),
    .data_in           (data_in),
    .frame_can_accept  (fft_frame_can_accept),
    .out_valid         (frame_valid),
    .data_out          (frame_data),
    .frame_drop_pulse  (frame_drop_pulse),
    .frame_drop_count  (frame_drop_count)
);

// -------------------------------------------------------------------------
// Blackman-Harris加窗
// -------------------------------------------------------------------------
wire [63:0] window_data;
wire        window_valid;
wire        window_last;

window_rom u_window_rom (
    .clk            (clk),
    .rst            (!rst_n),
    .data_in        (frame_data),
    .data_in_valid  (frame_valid),
    .data_out       (window_data),
    .data_out_valid (window_valid),
    .data_out_last  (window_last)
);

// -------------------------------------------------------------------------
// 1024点FFT
// -------------------------------------------------------------------------
// fft_result_*是xfft_1024内部原有的调试输出，当前数据通路不使用。
wire        fft_result_wen;
wire [63:0] fft_result_data;

xfft_1024 #(
    .FFT_POINTS (1024)
) u_xfft_1024 (
    .rst                  (!rst_n),
    .clk                  (clk),
    .overlap_wen          (window_valid),
    .overlap_data         (window_data),
    .m_axis_data_tready   (fft_raw_tready),
    .fft_result_wen       (fft_result_wen),
    .fft_result_data      (fft_result_data),
    .m_axis_data_tdata    (fft_raw_tdata),
    .m_axis_data_tvalid   (fft_raw_tvalid),
    .m_axis_data_tlast    (fft_raw_tlast),
    .frame_can_accept     (fft_frame_can_accept),
    .fifo_overflow_sticky (fft_fifo_overflow_sticky)
);

// -------------------------------------------------------------------------
// 多帧频谱最大保持
// -------------------------------------------------------------------------
// 最大保持模块向FFT提供fft_raw_tready，因此FFT只会在后级能够接收时前进。
fft_max_hold #(
    .FFT_POINTS (1024)
) u_fft_max_hold (
    .clk             (clk),
    .rst             (!rst_n),
    .s_axis_tdata    (fft_raw_tdata),
    .s_axis_tvalid   (fft_raw_tvalid),
    .s_axis_tready   (fft_raw_tready),
    .s_axis_tlast    (fft_raw_tlast),
    .hold_frames_cfg (max_hold_frames_cfg),
    .m_axis_tdata    (m_axis_data_tdata),
    .m_axis_tvalid   (m_axis_data_tvalid),
    .m_axis_tready   (m_axis_data_tready),
    .m_axis_tlast    (m_axis_data_tlast)
);

endmodule
