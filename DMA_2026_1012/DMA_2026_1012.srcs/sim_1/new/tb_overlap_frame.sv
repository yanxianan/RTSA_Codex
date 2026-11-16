`timescale 1ns / 1ps

// ============================================================================
// 模块名称：tb_overlap_frame_dds_modified
//
// 仿真目的：
//   1. 使用内部DDS给overlap_frame持续提供输入数据；
//   2. 依次切换4组CIC和HB配置，观察抽取倍率是否发生变化；
//   3. 验证组帧FIFO写时钟已经改为312.5 MHz的r_clk；
//   4. 验证每个抽取输出有效脉冲只触发一次FIFO写入；
//   5. 观察1024点、75%重叠组帧以及后续FFT是否仍能输出。
//
// 建议加入波形窗口的信号：
//   cic_rate_cfg、cic_trunc_cfg、hb_level、cic_cfg_valid、cic_cfg_busy；
//   dut.data_out_time_i、dut.data_out_time_q、dut.data_out_time_valid；
//   frame_fifo_wr_clk、frame_fifo_wr_en、dut.u_frame_ctrl.frame_ram_we；
//   dut.data_out_frame_valid、m_axis_data_tvalid、m_axis_data_tlast；
//   dec_valid_interval、dec_output_count、fifo_write_count。
//
// 四组配置对应的理论总抽取倍率：
//   CIC=4， HB=0：4倍；
//   CIC=4， HB=1：8倍；
//   CIC=8， HB=2：32倍；
//   CIC=16，HB=2：64倍。
// ============================================================================

module tb_overlap_frame;

// ----------------------------------------------------------------------------
// 时钟与复位
// ----------------------------------------------------------------------------
reg r_clk;
reg w_clk;
reg r_rst;
reg w_rst;

// r_clk为312.5 MHz，周期为3.2 ns。
always #1.600 r_clk = ~r_clk;

// 保留原来的312.5 MHz/32时钟作为对照。
// 修改后的组帧FIFO不再使用这个时钟写入数据。
always #51.200 w_clk = ~w_clk;

// ----------------------------------------------------------------------------
// DDS和抽取滤波配置
// ----------------------------------------------------------------------------
reg [3:0]  dma_mode;
reg        dds_resync;
reg [15:0] dds_phase_step_h;
reg [15:0] dds_phase_step_l;

reg [15:0] cic_rate_cfg;
reg [7:0]  cic_trunc_cfg;
reg [2:0]  hb_level;
reg        cic_cfg_valid;
wire       cic_cfg_busy;

// ----------------------------------------------------------------------------
// 本测试使用DDS模式，因此RFADC输入固定为0。
// ----------------------------------------------------------------------------
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

// FFT输出端始终允许接收数据，本TB不测试DMA反压。
reg         m_axis_data_tready;
wire [63:0] m_axis_data_tdata;
wire        m_axis_data_tvalid;
wire        m_axis_data_tlast;

// ----------------------------------------------------------------------------
// 被测模块
// ----------------------------------------------------------------------------
overlap_frame dut (
    .w_rst              (w_rst),
    .w_clk              (w_clk),
    .r_rst              (r_rst),
    .r_clk              (r_clk),

    .dma_mode           (dma_mode),
    .dds_resync         (dds_resync),
    .dds_phase_step_h   (dds_phase_step_h),
    .dds_phase_step_l   (dds_phase_step_l),

    .cic_rate_cfg       (cic_rate_cfg),
    .cic_trunc_cfg      (cic_trunc_cfg),
    .hb_level           (hb_level),
    .cic_cfg_valid      (cic_cfg_valid),
    .cic_cfg_busy       (cic_cfg_busy),

    .m00_axis_tdata     (m00_axis_tdata),
    .m00_axis_tvalid    (m00_axis_tvalid),
    .m00_axis_tready    (m00_axis_tready),
    .m01_axis_tdata     (m01_axis_tdata),
    .m01_axis_tvalid    (m01_axis_tvalid),
    .m01_axis_tready    (m01_axis_tready),
    .m10_axis_tdata     (m10_axis_tdata),
    .m10_axis_tvalid    (m10_axis_tvalid),
    .m10_axis_tready    (m10_axis_tready),
    .m11_axis_tdata     (m11_axis_tdata),
    .m11_axis_tvalid    (m11_axis_tvalid),
    .m11_axis_tready    (m11_axis_tready),

    .m_axis_data_tready (m_axis_data_tready),
    .m_axis_data_tdata  (m_axis_data_tdata),
    .m_axis_data_tvalid (m_axis_data_tvalid),
    .m_axis_data_tlast  (m_axis_data_tlast)
);

// ----------------------------------------------------------------------------
// 便于在波形中直接观察组帧FIFO的写时钟和写使能。
//
// 正确结果应满足：
//   frame_fifo_wr_clk始终与r_clk相同；
//   frame_fifo_wr_en与抽取滤波输出有效脉冲一一对应；
//   w_clk虽然仍在运行，但不再决定FIFO何时写入。
// ----------------------------------------------------------------------------
wire frame_fifo_wr_clk = dut.u_frame_ctrl.async_fifo_u.wr_clk;
wire frame_fifo_wr_en  = dut.u_frame_ctrl.async_fifo_u.wr_en;

// ----------------------------------------------------------------------------
// 波形观察计数器
// ----------------------------------------------------------------------------
integer r_clk_cycle_count;
integer previous_dec_valid_cycle;
integer dec_valid_interval;
integer dec_output_count;
integer fifo_write_count;
integer frame_ram_write_count;
integer frame_output_beat_count;
integer fft_output_beat_count;
integer fft_output_frame_count;
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
        frame_ram_write_count    = 0;
        frame_output_beat_count  = 0;
        fft_output_beat_count    = 0;
        fft_output_frame_count   = 0;
    end
    else begin
        if (dut.data_out_time_valid) begin
            dec_output_count = dec_output_count + 1;

            if (previous_dec_valid_cycle >= 0)
                dec_valid_interval = r_clk_cycle_count -
                                     previous_dec_valid_cycle;

            previous_dec_valid_cycle = r_clk_cycle_count;
        end

        if (frame_fifo_wr_en)
            fifo_write_count = fifo_write_count + 1;

        if (dut.u_frame_ctrl.frame_ram_we)
            frame_ram_write_count = frame_ram_write_count + 1;

        if (dut.data_out_frame_valid)
            frame_output_beat_count = frame_output_beat_count + 1;

        if (m_axis_data_tvalid && m_axis_data_tready) begin
            fft_output_beat_count = fft_output_beat_count + 1;

            if (m_axis_data_tlast)
                fft_output_frame_count = fft_output_frame_count + 1;
        end
    end
end

// 每个抽取输出有效脉冲必须对应一次FIFO写入，不能重复写也不能漏写。
always @(posedge r_clk) begin
    if (r_rst && (dma_mode == 4'd1) && dut.datapath_r_rst_n) begin
        if (frame_fifo_wr_en !== dut.data_out_time_valid) begin
            $error("[%0t] frame FIFO write enable does not match decimator valid",
                   $time);
            error_count = error_count + 1;
        end
    end
end

// ----------------------------------------------------------------------------
// 修改CIC和HB配置
// ----------------------------------------------------------------------------
task change_filter_config;
    input [15:0] new_cic_rate;
    input [7:0]  new_cic_trunc;
    input [2:0]  new_hb_level;
    input integer new_total_decimation;
    begin
        // 等待上一轮配置结束。
        while (cic_cfg_busy !== 1'b0)
            @(negedge r_clk);

        @(negedge r_clk);
        cic_rate_cfg              = new_cic_rate;
        cic_trunc_cfg             = new_cic_trunc;
        hb_level                  = new_hb_level;
        expected_total_decimation = new_total_decimation;

        // 配置有效保持一个完整的r_clk周期。
        @(negedge r_clk);
        cic_cfg_valid = 1'b1;
        @(negedge r_clk);
        cic_cfg_valid = 1'b0;

        // 等待I/Q两路CIC完成配置。
        while (cic_cfg_busy !== 1'b1)
            @(negedge r_clk);
        while (cic_cfg_busy !== 1'b0)
            @(negedge r_clk);

        // 给HB切换、FIFO排空和滤波流水线留出时间。
        repeat (32) @(negedge r_clk);

        segment_counter_reset = 1'b1;
        @(negedge r_clk);
        segment_counter_reset = 1'b0;

        $display("[%0t] CIC=%0d, TRUNC=%0d, HB=%0d, TOTAL_DEC=%0d",
                 $time, new_cic_rate, new_cic_trunc,
                 new_hb_level, new_total_decimation);
    end
endtask

// 输出每个倍率测试阶段的统计结果。
task report_current_segment;
    begin
        $display("------------------------------------------------------------");
        $display("TOTAL_DEC             = %0d", expected_total_decimation);
        $display("last valid interval   = %0d r_clk cycles",
                 dec_valid_interval);
        $display("decimator outputs     = %0d", dec_output_count);
        $display("FIFO writes           = %0d", fifo_write_count);
        $display("frame RAM writes      = %0d", frame_ram_write_count);
        $display("frame output beats    = %0d", frame_output_beat_count);
        $display("FFT output beats      = %0d", fft_output_beat_count);
        $display("FFT output frames     = %0d", fft_output_frame_count);

        if (dec_output_count == 0) begin
            $error("No decimator output was observed in this segment");
            error_count = error_count + 1;
        end

        if (fifo_write_count != dec_output_count) begin
            $error("FIFO write count %0d does not match decimator output count %0d",
                   fifo_write_count, dec_output_count);
            error_count = error_count + 1;
        end
    end
endtask

// ----------------------------------------------------------------------------
// 主测试流程
// ----------------------------------------------------------------------------
initial begin
    r_clk                     = 1'b0;
    w_clk                     = 1'b0;
    r_rst                     = 1'b0;
    w_rst                     = 1'b0;
    dma_mode                  = 4'd0;
    dds_resync                = 1'b0;
    dds_phase_step_h          = 16'h004E;
    dds_phase_step_l          = 16'hA4A9;
    cic_rate_cfg              = 16'd8;
    cic_trunc_cfg             = 8'd31;
    hb_level                  = 3'd2;
    cic_cfg_valid             = 1'b0;
    m_axis_data_tready        = 1'b1;
    m00_axis_tdata            = 64'd0;
    m00_axis_tvalid           = 1'b0;
    m01_axis_tdata            = 64'd0;
    m01_axis_tvalid           = 1'b0;
    m10_axis_tdata            = 64'd0;
    m10_axis_tvalid           = 1'b0;
    m11_axis_tdata            = 64'd0;
    m11_axis_tvalid           = 1'b0;
    r_clk_cycle_count         = 0;
    previous_dec_valid_cycle = -1;
    dec_valid_interval        = 0;
    dec_output_count          = 0;
    fifo_write_count          = 0;
    frame_ram_write_count     = 0;
    frame_output_beat_count   = 0;
    fft_output_beat_count     = 0;
    fft_output_frame_count    = 0;
    expected_total_decimation = 32;
    error_count               = 0;
    segment_counter_reset     = 1'b0;

    // 两个外部时钟域分别释放复位。
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

    // 选择内部DDS，等待模式切换复位结束。
    dma_mode = 4'd1;
    repeat (80) @(negedge r_clk);

    dds_resync = 1'b1;
    @(negedge r_clk);
    dds_resync = 1'b0;

    // 第1组：总抽取4倍。
    change_filter_config(16'd4, 8'd25, 3'd0, 4);
    repeat (10000) @(negedge r_clk);
    report_current_segment();

    // 第2组：总抽取8倍。
    change_filter_config(16'd4, 8'd25, 3'd1, 8);
    repeat (16000) @(negedge r_clk);
    report_current_segment();

    // 第3组：总抽取32倍，等于原来的固定倍率。
    change_filter_config(16'd8, 8'd31, 3'd2, 32);
    repeat (40000) @(negedge r_clk);
    report_current_segment();

    // 第4组：总抽取64倍，验证输出速率低于原固定写时钟时不会重复写入。
    change_filter_config(16'd16, 8'd35, 3'd2, 64);
    repeat (80000) @(negedge r_clk);
    report_current_segment();

    repeat (100) @(negedge r_clk);

    $display("============================================================");
    if (error_count == 0)
        $display("PASS: variable decimation and frame FIFO write path completed");
    else
        $display("FAIL: %0d error(s) detected", error_count);
    $display("============================================================");

    $finish;
end

endmodule
