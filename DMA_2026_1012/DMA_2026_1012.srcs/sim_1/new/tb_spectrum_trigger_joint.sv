`timescale 1ns / 1ps

//==============================================================================
// 频谱处理与触发通路联合仿真
//
// 连接方式：
// trigger_module已经集成在spectrum_engine内部；
// TB直接配置spectrum_engine的触发端口并检查触发捕获AXI输出。
//
// 验证内容：
// 1. DDS、抽取、重叠组帧、FFT和最大保持通路能够正常运行；
// 2. 捕获模块积累够触发前历史数据后，FFT功率越限产生一次触发；
// 3. 捕获输出数量等于触发前点数与触发后点数之和；
// 4. 捕获输出与实际抽取时域IQ逐点一致，时间顺序没有错位；
// 5. AXI反压期间，触发捕获输出数据和控制信号保持稳定；
// 6. 实时频谱输出与触发捕获输出能够同时完成。
//==============================================================================
module tb_spectrum_trigger_joint;

localparam integer PRE_SAMPLES   = 64;
localparam integer POST_SAMPLES  = 32;
localparam integer TOTAL_SAMPLES = PRE_SAMPLES + POST_SAMPLES;
localparam integer SAMPLE_LOG_DEPTH = 65536;

// -------------------------------------------------------------------------
// 时钟与复位
// -------------------------------------------------------------------------
reg r_clk;
reg w_clk;
reg r_rst;
reg w_rst;

// 312.5 MHz，周期3.2 ns。
always #1.600 r_clk = ~r_clk;

// 兼容保留时钟，当前频谱数据通路不再使用它。
always #51.200 w_clk = ~w_clk;

// -------------------------------------------------------------------------
// spectrum_engine配置
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

// 本测试使用内部DDS，RFADC输入固定为0。
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

// 实时最大保持频谱输出始终允许传输。
reg         spectrum_tready;
wire [63:0] spectrum_tdata;
wire        spectrum_tvalid;
wire        spectrum_tlast;

// 频率触发配置。
reg         trigger_enable;
reg         trigger_clear;
reg  [9:0]  start_bin;
reg  [9:0]  end_bin;
reg  [63:0] power_threshold;

// 触发结果。
wire        trigger_pulse;
wire        trigger_latched;
wire [9:0]  trigger_bin;
wire [63:0] trigger_power;
wire [63:0] trigger_threshold;

// 时域捕获配置。
reg         capture_arm;
reg         capture_clear;
reg  [31:0] pre_sample_num;
reg  [31:0] post_sample_num;

// 触发捕获AXI4-Stream输出。
wire [63:0] trigger_axis_tdata;
wire [7:0]  trigger_axis_tkeep;
wire        trigger_axis_tvalid;
reg         trigger_axis_tready;
wire        trigger_axis_tlast;

// 触发捕获状态。
wire        capture_busy;
wire        capture_ready;
wire        capture_triggered;
wire        capture_done;
wire [31:0] captured_sample_num;

spectrum_engine u_spectrum_engine (
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

    .trigger_enable            (trigger_enable),
    .trigger_clear             (trigger_clear),
    .trigger_start_bin         (start_bin),
    .trigger_end_bin           (end_bin),
    .trigger_power_threshold   (power_threshold),

    .capture_arm               (capture_arm),
    .capture_clear             (capture_clear),
    .pre_sample_num            (pre_sample_num),
    .post_sample_num           (post_sample_num),

    .trigger_pulse             (trigger_pulse),
    .trigger_latched           (trigger_latched),
    .trigger_bin               (trigger_bin),
    .trigger_power             (trigger_power),
    .trigger_threshold         (trigger_threshold),
    .capture_busy              (capture_busy),
    .capture_ready             (capture_ready),
    .capture_triggered         (capture_triggered),
    .capture_done              (capture_done),
    .captured_sample_num       (captured_sample_num),

    .trigger_axis_tready       (trigger_axis_tready),
    .trigger_axis_tdata        (trigger_axis_tdata),
    .trigger_axis_tkeep        (trigger_axis_tkeep),
    .trigger_axis_tvalid       (trigger_axis_tvalid),
    .trigger_axis_tlast        (trigger_axis_tlast),

    .m_axis_data_tready        (spectrum_tready),
    .m_axis_data_tdata         (spectrum_tdata),
    .m_axis_data_tvalid        (spectrum_tvalid),
    .m_axis_data_tlast         (spectrum_tlast)
);

// -------------------------------------------------------------------------
// 从spectrum_engine内部读取抽取时域IQ，仅用于建立TB参考数据。
// 这些层级观察信号不参与工程综合。
// -------------------------------------------------------------------------
wire        datapath_rst_n = u_spectrum_engine.datapath_r_rst_n;
wire [63:0] time_data      = u_spectrum_engine.dec_data;
wire        time_valid     = u_spectrum_engine.dec_valid;

// -------------------------------------------------------------------------
// 记录抽取后所有有效IQ点，用于检查触发捕获输出的时间顺序。
// -------------------------------------------------------------------------
reg [63:0] sample_log [0:SAMPLE_LOG_DEPTH-1];
integer sample_count;
integer expected_first_index;
integer trigger_pulse_count;
integer error_count;

always @(posedge r_clk) begin
    if (!datapath_rst_n) begin
        sample_count         = 0;
        expected_first_index = -1;
        trigger_pulse_count  = 0;
    end
    else begin
        // 与trigger_capture相同，在本上升沿采入的有效点先计入历史数据。
        if (time_valid) begin
            if (sample_count < SAMPLE_LOG_DEPTH)
                sample_log[sample_count] = time_data;
            else begin
                $error("TB sample_log容量不足");
                error_count = error_count + 1;
            end

            sample_count = sample_count + 1;
        end

        // trigger_capture在该上升沿看到trigger_pulse，并确定触发边界。
        if (trigger_pulse) begin
            trigger_pulse_count  = trigger_pulse_count + 1;
            expected_first_index = sample_count - PRE_SAMPLES;

            $display("[%0t] trigger: bin=%0d, power=%0d, first_sample=%0d",
                     $time, trigger_bin, trigger_power,
                     expected_first_index);
        end
    end
end

// -------------------------------------------------------------------------
// 模拟触发数据DMA反压：每9拍暂停接收2拍。
// -------------------------------------------------------------------------
integer ready_cycle_count;

always @(posedge r_clk) begin
    if (!datapath_rst_n) begin
        ready_cycle_count   <= 0;
        trigger_axis_tready <= 1'b0;
    end
    else begin
        ready_cycle_count <= ready_cycle_count + 1;

        if (((ready_cycle_count % 9) == 4) ||
            ((ready_cycle_count % 9) == 5))
            trigger_axis_tready <= 1'b0;
        else
            trigger_axis_tready <= 1'b1;
    end
end

// -------------------------------------------------------------------------
// 逐点检查触发捕获输出。
// -------------------------------------------------------------------------
integer trigger_output_count;

always @(posedge r_clk) begin
    if (!datapath_rst_n) begin
        trigger_output_count <= 0;
    end
    else if (trigger_axis_tvalid && trigger_axis_tready) begin
        if (expected_first_index < 0) begin
            $error("尚未接受触发就出现了捕获输出");
            error_count = error_count + 1;
        end
        else if (trigger_axis_tdata !==
                 sample_log[expected_first_index + trigger_output_count]) begin
            $error("捕获数据错误：index=%0d, expected=%h, actual=%h",
                   trigger_output_count,
                   sample_log[expected_first_index + trigger_output_count],
                   trigger_axis_tdata);
            error_count = error_count + 1;
        end

        if (trigger_axis_tkeep !== 8'hFF) begin
            $error("触发输出TKEEP错误：index=%0d", trigger_output_count);
            error_count = error_count + 1;
        end

        if (trigger_axis_tlast !==
            (trigger_output_count == TOTAL_SAMPLES - 1)) begin
            $error("触发输出TLAST位置错误：index=%0d, tlast=%0b",
                   trigger_output_count, trigger_axis_tlast);
            error_count = error_count + 1;
        end

        trigger_output_count <= trigger_output_count + 1;
    end
end

// AXI反压期间，TDATA、TKEEP、TVALID和TLAST必须保持稳定。
reg        stall_active;
reg [63:0] stall_tdata;
reg [7:0]  stall_tkeep;
reg        stall_tlast;

always @(posedge r_clk) begin
    if (!datapath_rst_n) begin
        stall_active <= 1'b0;
        stall_tdata  <= 64'd0;
        stall_tkeep  <= 8'd0;
        stall_tlast  <= 1'b0;
    end
    else begin
        if (stall_active) begin
            if (!trigger_axis_tvalid ||
                (trigger_axis_tdata !== stall_tdata) ||
                (trigger_axis_tkeep !== stall_tkeep) ||
                (trigger_axis_tlast !== stall_tlast)) begin
                $error("AXI反压期间触发输出没有保持稳定");
                error_count = error_count + 1;
            end
        end

        if (trigger_axis_tvalid && !trigger_axis_tready) begin
            stall_active <= 1'b1;
            stall_tdata  <= trigger_axis_tdata;
            stall_tkeep  <= trigger_axis_tkeep;
            stall_tlast  <= trigger_axis_tlast;
        end
        else begin
            stall_active <= 1'b0;
        end
    end
end

// 统计实时频谱输出帧数，确认触发通路没有影响原实时通路。
integer spectrum_beat_count;
integer spectrum_frame_count;

always @(posedge r_clk) begin
    if (!datapath_rst_n) begin
        spectrum_beat_count  <= 0;
        spectrum_frame_count <= 0;
    end
    else if (spectrum_tvalid && spectrum_tready) begin
        spectrum_beat_count <= spectrum_beat_count + 1;

        if (spectrum_tlast)
            spectrum_frame_count <= spectrum_frame_count + 1;
    end
end

// -------------------------------------------------------------------------
// CIC动态配置任务
// -------------------------------------------------------------------------
task configure_cic;
    begin
        while (cic_cfg_busy !== 1'b0)
            @(negedge r_clk);

        @(negedge r_clk);
        cic_cfg_valid = 1'b1;
        @(negedge r_clk);
        cic_cfg_valid = 1'b0;

        while (cic_cfg_busy !== 1'b1)
            @(negedge r_clk);
        while (cic_cfg_busy !== 1'b0)
            @(negedge r_clk);
    end
endtask

// capture_arm采用上升沿武装。
task arm_capture;
    begin
        @(negedge r_clk);
        capture_arm = 1'b1;
        @(negedge r_clk);
        capture_arm = 1'b0;
    end
endtask

// -------------------------------------------------------------------------
// 主测试流程
// -------------------------------------------------------------------------
initial begin
    r_clk                   = 1'b0;
    w_clk                   = 1'b0;
    r_rst                   = 1'b0;
    w_rst                   = 1'b0;

    dma_mode                = 4'd0;
    dds_resync              = 1'b0;
    dds_phase_step_h        = 16'h004E;
    dds_phase_step_l        = 16'hA4A9;
    cic_rate_cfg            = 16'd8;
    cic_trunc_cfg           = 8'd31;
    hb_level                = 3'd2;
    max_hold_frames_cfg     = 16'd1;
    cic_cfg_valid           = 1'b0;

    m00_axis_tdata          = 64'd0;
    m00_axis_tvalid         = 1'b0;
    m01_axis_tdata          = 64'd0;
    m01_axis_tvalid         = 1'b0;
    m10_axis_tdata          = 64'd0;
    m10_axis_tvalid         = 1'b0;
    m11_axis_tdata          = 64'd0;
    m11_axis_tvalid         = 1'b0;

    spectrum_tready         = 1'b1;

    trigger_enable          = 1'b0;
    trigger_clear           = 1'b0;
    start_bin               = 10'd0;
    end_bin                 = 10'd1023;
    power_threshold         = 64'd0;

    capture_arm             = 1'b0;
    capture_clear           = 1'b0;
    pre_sample_num          = PRE_SAMPLES;
    post_sample_num         = POST_SAMPLES;
    trigger_axis_tready     = 1'b0;

    sample_count            = 0;
    expected_first_index    = -1;
    trigger_pulse_count     = 0;
    trigger_output_count    = 0;
    ready_cycle_count       = 0;
    spectrum_beat_count     = 0;
    spectrum_frame_count    = 0;
    error_count             = 0;
    stall_active            = 1'b0;
    stall_tdata             = 64'd0;
    stall_tkeep             = 8'd0;
    stall_tlast             = 1'b0;

    // 释放复位。
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

    // 选择内部DDS并等待模式切换复位结束。
    dma_mode = 4'd1;
    wait (datapath_rst_n);
    repeat (16) @(negedge r_clk);

    configure_cic();

    dds_resync = 1'b1;
    @(negedge r_clk);
    dds_resync = 1'b0;

    // 先武装捕获，再允许频率触发。
    // 门限为0且监测全频段，首个非零FFT频点应产生触发。
    arm_capture();
    trigger_enable = 1'b1;

    // 等待捕获完成以及至少一帧实时频谱输出，设置超时保护。
    fork : WAIT_RESULT
        begin
            wait (capture_done && (spectrum_frame_count >= 1));
        end
        begin
            repeat (300000) @(posedge r_clk);
            $fatal(1, "联合仿真超时：未完成触发捕获或实时频谱输出");
        end
    join_any
    disable WAIT_RESULT;

    repeat (10) @(posedge r_clk);

    if (trigger_pulse_count != 1) begin
        $error("触发脉冲数量错误：期望1，实际%0d", trigger_pulse_count);
        error_count = error_count + 1;
    end

    if (!trigger_latched || !capture_triggered) begin
        $error("触发状态没有正确锁存");
        error_count = error_count + 1;
    end

    if (trigger_power <= trigger_threshold) begin
        $error("触发功率没有超过门限：power=%0d, threshold=%0d",
               trigger_power, trigger_threshold);
        error_count = error_count + 1;
    end

    if (captured_sample_num != TOTAL_SAMPLES) begin
        $error("捕获长度错误：期望%0d，实际%0d",
               TOTAL_SAMPLES, captured_sample_num);
        error_count = error_count + 1;
    end

    if (trigger_output_count != TOTAL_SAMPLES) begin
        $error("触发AXI输出数量错误：期望%0d，实际%0d",
               TOTAL_SAMPLES, trigger_output_count);
        error_count = error_count + 1;
    end

    if (spectrum_frame_count < 1) begin
        $error("没有观察到实时频谱输出帧");
        error_count = error_count + 1;
    end

    if (u_spectrum_engine.fft_fifo_overflow_sticky !== 1'b0) begin
        $error("FFT输入FIFO发生溢出");
        error_count = error_count + 1;
    end

    $display("============================================================");
    $display("trigger bin          = %0d", trigger_bin);
    $display("trigger power        = %0d", trigger_power);
    $display("captured samples     = %0d", captured_sample_num);
    $display("trigger output beats = %0d", trigger_output_count);
    $display("spectrum frames      = %0d", spectrum_frame_count);

    if (error_count == 0)
        $display("PASS: spectrum_engine and trigger_module joint test passed");
    else
        $fatal(1, "FAIL: joint test detected %0d error(s)", error_count);

    $display("============================================================");
    $finish;
end

endmodule
