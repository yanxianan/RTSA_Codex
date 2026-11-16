`timescale 1ns / 1ps

// ============================================================================
// spectrum_engine仿真
//
// 仿真目的：
// 1. 使用内部DDS产生连续IQ数据；
// 2. 依次测试4组CIC和HB抽取配置；
// 3. 验证抽取有效脉冲与组帧FIFO写入一一对应；
// 4. 验证重叠组帧、FFT以及最大保持之后能够正常输出完整频谱帧。
// ============================================================================
module tb_spectrum_engine;

// -------------------------------------------------------------------------
// 时钟与复位
// -------------------------------------------------------------------------
reg r_clk;
reg w_clk;
reg r_rst;
reg w_rst;

// r_clk为312.5 MHz，周期3.2 ns。
always #1.600 r_clk = ~r_clk;

// 兼容保留时钟。当前数据通路已经不再使用该时钟。
always #51.200 w_clk = ~w_clk;

// -------------------------------------------------------------------------
// DDS、CIC、HB和最大保持配置
// -------------------------------------------------------------------------
reg  [3:0]  dma_mode;
reg         dds_resync;
reg  [15:0] dds_phase_step_h;
reg  [15:0] dds_phase_step_l;
reg  [15:0] cic_rate_cfg;
reg  [7:0]  cic_trunc_cfg;
reg  [2:0]  hb_level;
reg  [15:0] max_hold_frames_cfg;
reg         cic_cfg_valid;
wire        cic_cfg_busy;

// -------------------------------------------------------------------------
// 本测试使用DDS模式，因此四路RFADC输入固定为0。
// -------------------------------------------------------------------------
reg  [63:0] m00_axis_tdata;
reg         m00_axis_tvalid;
wire        m00_axis_tready;
reg  [63:0] m01_axis_tdata;
reg         m01_axis_tvalid;
wire        m01_axis_tready;
reg  [63:0] m10_axis_tdata;
reg         m10_axis_tvalid;
wire        m10_axis_tready;
reg  [63:0] m11_axis_tdata;
reg         m11_axis_tvalid;
wire        m11_axis_tready;

// 本TB暂不测试DMA反压，频谱输出端始终可以接收数据。
reg         m_axis_data_tready;
wire [63:0] m_axis_data_tdata;
wire        m_axis_data_tvalid;
wire        m_axis_data_tlast;

// -------------------------------------------------------------------------
// 被测模块
// -------------------------------------------------------------------------
spectrum_engine dut (
    .w_rst                     (w_rst),
    .w_clk                     (w_clk),
    .r_rst                     (r_rst),
    .r_clk                     (r_clk),

    .dma_mode                  (dma_mode),
    .dds_resync                (dds_resync),
    .dds_phase_step_h          (dds_phase_step_h),
    .dds_phase_step_l          (dds_phase_step_l),

    .cic_rate_cfg              (cic_rate_cfg),
    .cic_trunc_cfg             (cic_trunc_cfg),
    .hb_level                  (hb_level),
    .max_hold_frames_cfg       (max_hold_frames_cfg),
    .cic_cfg_valid             (cic_cfg_valid),
    .cic_cfg_busy              (cic_cfg_busy),
    .config_datapath_reset     (1'b0),

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
    .m_axis_data_tvalid        (m_axis_data_tvalid),
    .m_axis_data_tlast         (m_axis_data_tlast)
);

// -------------------------------------------------------------------------
// 将新层级中的关键信号引到TB顶层，方便加入Vivado波形窗口。
// -------------------------------------------------------------------------
wire        datapath_rst_n_dbg = dut.datapath_r_rst_n;
wire [63:0] dec_data_dbg       = dut.dec_data;
wire        dec_valid_dbg      = dut.dec_valid;

wire frame_fifo_wr_clk =
    dut.u_overlap_fft_module.u_frame_ctrl.async_fifo_u.wr_clk;
wire frame_fifo_wr_en =
    dut.u_overlap_fft_module.u_frame_ctrl.async_fifo_u.wr_en;
wire frame_ram_we =
    dut.u_overlap_fft_module.u_frame_ctrl.frame_ram_we;
wire frame_valid_dbg = dut.u_overlap_fft_module.frame_valid;

wire [63:0] fft_raw_tdata_dbg  = dut.fft_raw_tdata;
wire        fft_raw_tvalid_dbg = dut.fft_raw_tvalid;
wire        fft_raw_tready_dbg = dut.fft_raw_tready;
wire        fft_raw_tlast_dbg  = dut.fft_raw_tlast;

wire [31:0] frame_drop_count_dbg = dut.frame_drop_count;
wire fft_fifo_overflow_dbg = dut.fft_fifo_overflow_sticky;

// -------------------------------------------------------------------------
// 统计计数器
// -------------------------------------------------------------------------
integer r_clk_cycle_count;
integer previous_dec_valid_cycle;
integer dec_valid_interval;
integer dec_output_count;
integer fifo_write_count;
integer frame_output_beat_count;
integer fft_raw_beat_count;
integer fft_raw_frame_count;
integer spectrum_beat_count;
integer spectrum_frame_count;
integer expected_total_decimation;
integer error_count;

reg segment_counter_reset;

always @(posedge r_clk) begin
    r_clk_cycle_count = r_clk_cycle_count + 1;

    if (!r_rst || segment_counter_reset) begin
        previous_dec_valid_cycle = -1;
        dec_valid_interval       = 0;
        dec_output_count         = 0;
        fifo_write_count         = 0;
        frame_output_beat_count  = 0;
        fft_raw_beat_count       = 0;
        fft_raw_frame_count      = 0;
        spectrum_beat_count      = 0;
        spectrum_frame_count     = 0;
    end
    else begin
        if (dec_valid_dbg) begin
            dec_output_count = dec_output_count + 1;

            if (previous_dec_valid_cycle >= 0)
                dec_valid_interval = r_clk_cycle_count -
                                     previous_dec_valid_cycle;

            previous_dec_valid_cycle = r_clk_cycle_count;
        end

        if (frame_fifo_wr_en)
            fifo_write_count = fifo_write_count + 1;

        if (frame_valid_dbg)
            frame_output_beat_count = frame_output_beat_count + 1;

        // AXI流只有在valid和ready同时为1时才真正传输一个FFT点。
        if (fft_raw_tvalid_dbg && fft_raw_tready_dbg) begin
            fft_raw_beat_count = fft_raw_beat_count + 1;

            if (fft_raw_tlast_dbg)
                fft_raw_frame_count = fft_raw_frame_count + 1;
        end

        if (m_axis_data_tvalid && m_axis_data_tready) begin
            spectrum_beat_count = spectrum_beat_count + 1;

            if (m_axis_data_tlast)
                spectrum_frame_count = spectrum_frame_count + 1;
        end
    end
end

// 抽取输出每出现一个有效脉冲，组帧FIFO必须正好写入一次。
always @(posedge r_clk) begin
    if (r_rst && datapath_rst_n_dbg && (dma_mode == 4'd1)) begin
        if (frame_fifo_wr_en !== dec_valid_dbg) begin
            $error("[%0t] frame FIFO write enable does not match decimator valid",
                   $time);
            error_count = error_count + 1;
        end
    end
end

// -------------------------------------------------------------------------
// 修改一组CIC和HB配置
// -------------------------------------------------------------------------
task change_filter_config;
    input [15:0] new_cic_rate;
    input [7:0]  new_cic_trunc;
    input [2:0]  new_hb_level;
    input integer new_total_decimation;
    begin
        // 等待上一轮CIC配置完成。
        while (cic_cfg_busy !== 1'b0)
            @(negedge r_clk);

        @(negedge r_clk);
        cic_rate_cfg              = new_cic_rate;
        cic_trunc_cfg             = new_cic_trunc;
        hb_level                  = new_hb_level;
        expected_total_decimation = new_total_decimation;

        // cic_cfg_valid保持一个完整时钟周期。
        @(negedge r_clk);
        cic_cfg_valid = 1'b1;
        @(negedge r_clk);
        cic_cfg_valid = 1'b0;

        // 等待I/Q两路CIC接收并完成新配置。
        while (cic_cfg_busy !== 1'b1)
            @(negedge r_clk);
        while (cic_cfg_busy !== 1'b0)
            @(negedge r_clk);

        // 等待滤波器内部流水线进入稳定状态。
        repeat (64) @(negedge r_clk);

        segment_counter_reset = 1'b1;
        @(negedge r_clk);
        segment_counter_reset = 1'b0;

        $display("[%0t] CIC=%0d, TRUNC=%0d, HB=%0d, TOTAL_DEC=%0d",
                 $time, new_cic_rate, new_cic_trunc,
                 new_hb_level, new_total_decimation);
    end
endtask

// 输出并检查当前倍率阶段的统计结果。
task report_current_segment;
    begin
        $display("------------------------------------------------------------");
        $display("TOTAL_DEC             = %0d", expected_total_decimation);
        $display("last valid interval   = %0d r_clk cycles",
                 dec_valid_interval);
        $display("decimator outputs     = %0d", dec_output_count);
        $display("FIFO writes           = %0d", fifo_write_count);
        $display("frame output beats    = %0d", frame_output_beat_count);
        $display("raw FFT beats         = %0d", fft_raw_beat_count);
        $display("raw FFT frames        = %0d", fft_raw_frame_count);
        $display("spectrum beats        = %0d", spectrum_beat_count);
        $display("spectrum frames       = %0d", spectrum_frame_count);
        $display("frame drop count      = %0d", frame_drop_count_dbg);
        $display("FFT FIFO overflow     = %0d", fft_fifo_overflow_dbg);

        if (dec_output_count == 0) begin
            $error("No decimator output was observed in this segment");
            error_count = error_count + 1;
        end

        if (dec_valid_interval != expected_total_decimation) begin
            $error("Decimator interval %0d does not match expected value %0d",
                   dec_valid_interval, expected_total_decimation);
            error_count = error_count + 1;
        end

        if (fifo_write_count != dec_output_count) begin
            $error("FIFO write count %0d does not match decimator output count %0d",
                   fifo_write_count, dec_output_count);
            error_count = error_count + 1;
        end

        if (fft_fifo_overflow_dbg !== 1'b0) begin
            $error("FFT input FIFO overflow was detected");
            error_count = error_count + 1;
        end
    end
endtask

// -------------------------------------------------------------------------
// 主测试流程
// -------------------------------------------------------------------------
initial begin
    r_clk                      = 1'b0;
    w_clk                      = 1'b0;
    r_rst                      = 1'b0;
    w_rst                      = 1'b0;
    dma_mode                   = 4'd0;
    dds_resync                 = 1'b0;
    dds_phase_step_h           = 16'h004E;
    dds_phase_step_l           = 16'hA4A9;
    cic_rate_cfg               = 16'd8;
    cic_trunc_cfg              = 8'd31;
    hb_level                   = 3'd2;
    max_hold_frames_cfg        = 16'd1;
    cic_cfg_valid              = 1'b0;
    m_axis_data_tready         = 1'b1;
    m00_axis_tdata             = 64'd0;
    m00_axis_tvalid            = 1'b0;
    m01_axis_tdata             = 64'd0;
    m01_axis_tvalid            = 1'b0;
    m10_axis_tdata             = 64'd0;
    m10_axis_tvalid            = 1'b0;
    m11_axis_tdata             = 64'd0;
    m11_axis_tvalid            = 1'b0;
    r_clk_cycle_count          = 0;
    previous_dec_valid_cycle  = -1;
    dec_valid_interval         = 0;
    dec_output_count           = 0;
    fifo_write_count           = 0;
    frame_output_beat_count   = 0;
    fft_raw_beat_count         = 0;
    fft_raw_frame_count        = 0;
    spectrum_beat_count        = 0;
    spectrum_frame_count       = 0;
    expected_total_decimation  = 32;
    error_count                = 0;
    segment_counter_reset      = 1'b0;

    // 分别释放两个兼容时钟域的复位。
    fork
        begin
            repeat (20) @(negedge r_clk);
            r_rst = 1'b1;
        end
        begin
            repeat (4) @(negedge w_clk);
            w_rst = 1'b1;
        end
    join

    repeat (20) @(negedge r_clk);

    // 选择内部DDS，并等待数据源切换复位结束。
    dma_mode = 4'd1;
    repeat (80) @(negedge r_clk);

    dds_resync = 1'b1;
    @(negedge r_clk);
    dds_resync = 1'b0;

    // 第1组：CIC 4倍，HB不抽取，总抽取4倍。
    change_filter_config(16'd4, 8'd25, 3'd0, 4);
    repeat (10000) @(negedge r_clk);
    report_current_segment();

    // 第2组：CIC 4倍，一级HB 2倍，总抽取8倍。
    change_filter_config(16'd4, 8'd25, 3'd1, 8);
    repeat (16000) @(negedge r_clk);
    report_current_segment();

    // 第3组：CIC 8倍，两级HB 4倍，总抽取32倍。
    change_filter_config(16'd8, 8'd31, 3'd2, 32);
    repeat (40000) @(negedge r_clk);
    report_current_segment();

    // 第4组：CIC 16倍，两级HB 4倍，总抽取64倍。
    change_filter_config(16'd16, 8'd35, 3'd2, 64);
    repeat (80000) @(negedge r_clk);
    report_current_segment();

    repeat (100) @(negedge r_clk);

    $display("============================================================");
    if (error_count == 0)
        $display("PASS: spectrum_engine completed all four configurations");
    else
        $display("FAIL: %0d error(s) detected", error_count);
    $display("============================================================");

    $finish;
end

endmodule
