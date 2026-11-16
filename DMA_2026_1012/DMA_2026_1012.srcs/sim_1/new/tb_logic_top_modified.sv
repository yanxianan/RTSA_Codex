`timescale 1ns / 1ps

// logic_top联合仿真：检查上电默认80帧，再写成2帧，验证抽取、重叠帧和触发。
// TB从BRAM端口模拟PS写寄存器，不使用force修改业务信号。
module tb_logic_top_modified;

localparam integer FFT_POINTS = 1024;
localparam integer AXIS_HEADER_WORDS = 8;
localparam integer AXIS_PAYLOAD_WORDS = FFT_POINTS / 4;
localparam integer AXIS_FRAME_WORDS = AXIS_HEADER_WORDS + AXIS_PAYLOAD_WORDS;
localparam integer HOLD_FRAMES = 2;
localparam integer PRE_SAMPLES = 64;
localparam integer POST_SAMPLES = 32;
localparam integer CAPTURE_SAMPLES = PRE_SAMPLES + POST_SAMPLES;
localparam integer LOG_DEPTH = 8192;

reg logic_clk = 1'b0;
reg axi_bram_clk = 1'b0;
reg w_clk = 1'b0;
always #1.600 logic_clk = ~logic_clk;
always #5.000 axi_bram_clk = ~axi_bram_clk;
always #51.200 w_clk = ~w_clk;

reg r_rst = 1'b0;
reg w_rst = 1'b0;
reg axi_bram_rst = 1'b1;
reg [12:0] axi_bram_addr = 13'd0;
reg [31:0] axi_bram_wd_din = 32'd0;
reg axi_bram_en = 1'b0;
reg [3:0] axi_bram_we = 4'd0;
wire [31:0] axi_bram_rd_dout;

wire [63:0] m_axis_data_tdata;
wire [7:0] m_axis_data_tkeep;
wire m_axis_data_tvalid;
wire m_axis_data_tlast;
reg m_axis_data_tready = 1'b1;

logic_top dut (
    .logic_clk(logic_clk), .r_rst(r_rst), .w_clk(w_clk), .w_rst(w_rst),
    .axi_bram_addr(axi_bram_addr), .axi_bram_clk(axi_bram_clk),
    .axi_bram_wd_din(axi_bram_wd_din), .axi_bram_rd_dout(axi_bram_rd_dout),
    .axi_bram_en(axi_bram_en), .axi_bram_rst(axi_bram_rst),
    .axi_bram_we(axi_bram_we),
    .m00_axis_tdata(64'd0), .m00_axis_tvalid(1'b0), .m00_axis_tready(),
    .m01_axis_tdata(64'd0), .m01_axis_tvalid(1'b0), .m01_axis_tready(),
    .m10_axis_tdata(64'd0), .m10_axis_tvalid(1'b0), .m10_axis_tready(),
    .m11_axis_tdata(64'd0), .m11_axis_tvalid(1'b0), .m11_axis_tready(),
    .m_axis_data_tready(m_axis_data_tready),
    .m_axis_data_tdata(m_axis_data_tdata),
    .m_axis_data_tkeep(m_axis_data_tkeep),
    .m_axis_data_tvalid(m_axis_data_tvalid),
    .m_axis_data_tlast(m_axis_data_tlast),
    .reg_clk(), .reg_addr(), .reg_wr_en(), .reg_rd_en(), .reg_data()
);

// 观察业务链中的真实有效数据，不驱动这些内部信号。
wire datapath_rst_n = dut.u_spectrum_engine.datapath_r_rst_n;
wire [63:0] dec_data = dut.u_spectrum_engine.dec_data;
wire dec_valid = dut.u_spectrum_engine.dec_valid;
wire [63:0] frame_data = dut.u_spectrum_engine.u_overlap_fft_module.frame_data;
wire frame_valid = dut.u_spectrum_engine.u_overlap_fft_module.frame_valid;
wire [63:0] raw_data = dut.u_spectrum_engine.fft_raw_tdata;
wire raw_valid = dut.u_spectrum_engine.fft_raw_tvalid;
wire raw_ready = dut.u_spectrum_engine.fft_raw_tready;
wire raw_last = dut.u_spectrum_engine.fft_raw_tlast;

// 模拟一次PS经AXI BRAM Controller发出的完整字写入。
// 当前bram_ctrl_intf会延长写使能，因此两次写入之间留出间隔。
task automatic write_reg(input [12:0] addr, input [31:0] value);
begin
    @(negedge axi_bram_clk);
    axi_bram_addr = addr;
    axi_bram_wd_din = value;
    axi_bram_en = 1'b1;
    axi_bram_we = 4'hF;
    @(negedge axi_bram_clk);
    axi_bram_en = 1'b0;
    axi_bram_we = 4'd0;
    repeat (18) @(negedge axi_bram_clk);
end
endtask

reg [63:0] first_frame [0:FFT_POINTS-1];
reg [63:0] max_data [0:FFT_POINTS-1];
reg [63:0] max_power [0:FFT_POINTS-1];
reg [63:0] sample_log [0:LOG_DEPTH-1];
integer cycles = 0;
integer previous_dec_cycle = -1;
integer dec_count = 0;
integer sample_count = 0;
integer frame_beat = 0;
integer frame_count = 0;
integer raw_beat = 0;
integer raw_frames = 0;
integer spectrum_beat = 0;
integer spectrum_frames = 0;
integer capture_beat = 0;
integer trigger_pulses = 0;
integer arm_pulses = 0;
integer expected_first_sample = -1;
integer peak_bin = 0;
integer i;
reg signed [63:0] real_value;
reg signed [63:0] imag_value;
reg [63:0] power_value;

always @(posedge logic_clk) begin
    cycles = cycles + 1;
    if (!datapath_rst_n) begin
        previous_dec_cycle = -1;
        dec_count = 0;
        sample_count = 0;
        frame_beat = 0;
        frame_count = 0;
        raw_beat = 0;
        raw_frames = 0;
        spectrum_beat = 0;
        spectrum_frames = 0;
        capture_beat = 0;
        trigger_pulses = 0;
        arm_pulses = 0;
        expected_first_sample = -1;
    end else begin
        // CIC 8倍、HB 4倍时，抽取后相邻有效点应相隔32个logic_clk周期。
        if (dec_valid) begin
            if ((dec_count > 20) &&
                ((cycles - previous_dec_cycle) != 32))
                $fatal(1, "抽取间隔错误：实际%0d拍，期望32拍",
                       cycles - previous_dec_cycle);
            previous_dec_cycle = cycles;
            sample_log[sample_count % LOG_DEPTH] = dec_data;
            sample_count = sample_count + 1;
            dec_count = dec_count + 1;
        end

        // 第二帧的前768点应与第一帧的后768点逐点相同。
        if (frame_valid) begin
            if (frame_count == 0)
                first_frame[frame_beat] = frame_data;
            if ((frame_count == 1) && (frame_beat < 768) &&
                (frame_data !== first_frame[frame_beat + 256]))
                $fatal(1, "重叠帧错误：第二帧第%0d点不等于第一帧第%0d点",
                       frame_beat, frame_beat + 256);
            frame_beat = frame_beat + 1;
            if (frame_beat == FFT_POINTS) begin
                frame_beat = 0;
                frame_count = frame_count + 1;
            end
        end

        // 用实际FFT输入建立2帧逐频点最大保持的参考结果。
        if (raw_valid && raw_ready) begin
            if (raw_last !== (raw_beat == FFT_POINTS - 1))
                $fatal(1, "原始FFT帧TLAST位置错误，频点=%0d", raw_beat);
            if (raw_frames < HOLD_FRAMES) begin
                if ((^raw_data) === 1'bx)
                    $fatal(1, "原始FFT有效数据含X，频点=%0d", raw_beat);
                real_value = $signed(raw_data[31:0]);
                imag_value = $signed(raw_data[63:32]);
                power_value = real_value * real_value + imag_value * imag_value;
                if ((raw_frames == 0) ||
                    (power_value > max_power[raw_beat])) begin
                    max_power[raw_beat] = power_value;
                    max_data[raw_beat] = raw_data;
                end
            end
            raw_beat = raw_beat + 1;
            if (raw_beat == FFT_POINTS) begin
                raw_beat = 0;
                raw_frames = raw_frames + 1;
            end
        end

        if (dut.capture_arm)
            arm_pulses = arm_pulses + 1;
        if (dut.trigger_pulse) begin
            trigger_pulses = trigger_pulses + 1;
            expected_first_sample = sample_count - PRE_SAMPLES;
        end

        // 默认DMA输出实时频谱；首包必须是2帧最大保持后的1024点。
        if (m_axis_data_tvalid && m_axis_data_tready) begin
            if (m_axis_data_tkeep !== 8'hFF)
                $fatal(1, "DMA输出TKEEP错误");
            if (!dut.dma_output_select) begin
                if (spectrum_frames == 0) begin
                    if (raw_frames < HOLD_FRAMES)
                        $fatal(1, "最大保持尚未累计2帧就开始输出");
                    if (spectrum_beat == 0
                        && m_axis_data_tdata[31:0] !== 32'h52545341)
                        $fatal(1, "频谱帧头 magic 错误: %h",
                               m_axis_data_tdata[31:0]);
                    if (spectrum_beat == 1
                        && m_axis_data_tdata[31:0] !== 32'd2112)
                        $fatal(1, "频谱帧头 frame_bytes 错误: %0d",
                               m_axis_data_tdata[31:0]);
                    if (spectrum_beat == 6
                        && m_axis_data_tdata[31:0] !== 32'd1024)
                        $fatal(1, "频谱帧头 bin_count 错误: %0d",
                               m_axis_data_tdata[31:0]);
                    if (m_axis_data_tlast !==
                        (spectrum_beat == AXIS_FRAME_WORDS - 1))
                        $fatal(1, "频谱帧TLAST位置错误，beat=%0d",
                               spectrum_beat);
                end
                if (spectrum_beat >= AXIS_HEADER_WORDS
                    && (^m_axis_data_tdata) === 1'bx)
                    $fatal(1, "频谱 payload 含 X，beat=%0d", spectrum_beat);
                spectrum_beat = spectrum_beat + 1;
                if (m_axis_data_tlast) begin
                    spectrum_frames = spectrum_frames + 1;
                    spectrum_beat = 0;
                end
            end else begin
                if (expected_first_sample < 0)
                    $fatal(1, "尚未触发就收到时域捕获数据");
                if (capture_beat >= CAPTURE_SAMPLES)
                    $fatal(1, "时域捕获输出超过配置点数");
                if (m_axis_data_tdata !==
                    sample_log[(expected_first_sample + capture_beat) % LOG_DEPTH])
                    $fatal(1, "时域捕获第%0d点与抽取输出不一致", capture_beat);
                if (m_axis_data_tlast !==
                    (capture_beat == CAPTURE_SAMPLES - 1))
                    $fatal(1, "时域捕获TLAST位置错误，点=%0d", capture_beat);
                capture_beat = capture_beat + 1;
            end
        end
    end
end

initial begin
    // 2帧累计和后续触发应在1毫秒内完成。
    #1_000_000;
    $fatal(1, "logic_top联合仿真超时：raw=%0d帧，max=%0d帧，捕获=%0d点",
           raw_frames, spectrum_frames, capture_beat);
end

initial begin
    repeat (10) @(negedge axi_bram_clk);
    axi_bram_rst = 1'b0;
    r_rst = 1'b1;
    w_rst = 1'b1;
    repeat (30) @(negedge logic_clk);

    if ((dut.dma_mode !== 4'd1) ||
        ({dut.dds_phase_step_h, dut.dds_phase_step_l} !== 32'h004EA4A9) ||
        (dut.cic_rate_cfg !== 16'd8) ||
        (dut.cic_trunc_cfg !== 8'd31) ||
        (dut.active_cic_cfg_valid !== 1'b0) ||
        (dut.hb_level !== 3'd2) ||
        (dut.max_hold_frames_cfg !== 16'd80) ||
        (dut.trigger_enable !== 1'b0) ||
        (dut.dma_output_select !== 1'b0))
        $fatal(1, "上电默认寄存器配置不符合预期，请确认已复制最新RTL");

    // 仿真期间用寄存器写入把最大保持80帧改为2帧。
    write_reg(13'h224, HOLD_FRAMES);
    // Max Hold is a soft parameter: stage + commit, applied at a frame boundary
    // without stopping DMA or resetting the PL datapath.
    write_reg(13'h120, 32'd0);
    write_reg(13'h120, 32'd1);
    wait (dut.max_hold_frames_cfg === 16'd2);
    wait (dut.active_max_hold_frames_cfg === 16'd2);

    wait (spectrum_frames >= 1);
    if ((frame_count < 2) || (dec_count < 1024) ||
        (dut.u_spectrum_engine.fft_fifo_overflow_sticky !== 1'b0))
        $fatal(1, "抽取、组帧或FFT缓存状态异常");
    $display("抽取、75%%重叠帧和2帧最大保持通过，耗时%0t ns", $time);

    // 从实际FFT数据中选出单音最强的频点，无需猜测IQ正负频率方向。
    peak_bin = 0;
    for (i = 1; i < FFT_POINTS; i = i + 1)
        if (max_power[i] > max_power[peak_bin])
            peak_bin = i;
    if (max_power[peak_bin] == 64'd0)
        $fatal(1, "FFT所有频点功率均为0，无法测试频率触发");
    // 抽取后采样率为9.765625 MHz，1.5 MHz约落在第157频点；兼容IQ方向反转。
    i = (peak_bin > 512) ? (FFT_POINTS - peak_bin) : peak_bin;
    if ((i < 154) || (i > 160))
        $fatal(1, "DDS主峰位置异常：bin=%0d，期望约为157或867", peak_bin);
    $display("1.5 MHz DDS对应的实测主峰：bin=%0d，功率=%0d",
             peak_bin, max_power[peak_bin]);

    // 保持频谱输出畅通，配置单频点、前64点、后32点。
    write_reg(13'h22C, (peak_bin << 16) | peak_bin);
    write_reg(13'h230, 32'hFFFFFFFF);
    write_reg(13'h234, 32'hFFFFFFFF);
    write_reg(13'h238, (POST_SAMPLES << 16) | PRE_SAMPLES);

    // 写bit1产生一次武装命令；先等触发前历史数据填满。
    write_reg(13'h228, 32'h00000002);
    wait (dut.capture_ready === 1'b1);
    write_reg(13'h228, 32'h00000001);

    // 全1门限下继续接收至少两帧原始FFT，必须保持不触发。
    begin : high_threshold_check
        integer first_raw_frame;
        first_raw_frame = raw_frames;
        wait (raw_frames >= first_raw_frame + 2);
    end
    if ((trigger_pulses != 0) || dut.trigger_latched || dut.capture_triggered)
        $fatal(1, "高门限下发生了误触发");

    // 关闭触发后把门限改为0，再打开触发，避免门限两半更新期间误触发。
    write_reg(13'h228, 32'h00000000);
    write_reg(13'h234, 32'd0);
    write_reg(13'h230, 32'd0);
    write_reg(13'h228, 32'h00000001);

    // 收到触发后再选时域输出，避免2帧最大保持因未被选中而堵住FFT。
    wait (trigger_pulses >= 1);
    write_reg(13'h228, 32'h00000009);

    wait (dut.capture_done === 1'b1 && capture_beat == CAPTURE_SAMPLES);
    repeat (5) @(negedge logic_clk);
    if ((trigger_pulses != 1) || !dut.trigger_latched ||
        !dut.capture_triggered || (dut.trigger_bin != peak_bin) ||
        (dut.trigger_power <= dut.trigger_threshold) ||
        (dut.captured_sample_num != CAPTURE_SAMPLES))
        $fatal(1, "频率触发或前后点数检查失败");
    if (arm_pulses != 1)
        $fatal(1, "一次BRAM武装写入产生了%0d个capture_arm脉冲，请检查bram_ctrl_intf写使能延长逻辑",
               arm_pulses);

    $display("PASS：logic_top默认值、抽取、重叠帧、最大保持、频率触发和时域捕获均通过");
    $finish;
end

endmodule
