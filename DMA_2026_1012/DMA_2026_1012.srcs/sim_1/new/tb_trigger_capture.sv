`timescale 1ns / 1ps

//==============================================================================
// trigger_capture自检测试
//
// 测试内容：
//   1. 使用实际8192×64位RAM，配置触发前8点、触发后4点；
//   2. 输入数据为连续递增序号，便于直接判断时间顺序；
//   3. 在触发前历史不足时发送一次触发，检查该触发被忽略；
//   4. 等环形RAM发生回绕后再次产生触发；
//   5. DMA侧周期性反压，检查输出数据在反压时保持稳定；
//   6. 检查输出数量、数据内容、TKEEP和TLAST位置。
//==============================================================================
module tb_trigger_capture;

reg                      clk;
reg                      rst;
reg  [63:0]               data_in;
reg                      data_in_valid;
reg                      trigger_in;
reg                      capture_arm;
reg                      capture_clear;
reg  [31:0]              pre_sample_num;
reg  [31:0]              post_sample_num;
wire [63:0]               m_axis_tdata;
wire [7:0]                m_axis_tkeep;
wire                     m_axis_tvalid;
reg                      m_axis_tready;
wire                     m_axis_tlast;
wire                     capture_busy;
wire                     capture_ready;
wire                     capture_triggered;
wire                     capture_done;
wire [31:0]              captured_sample_num;

integer error_count;
integer input_index;
integer output_index;
integer cycle_count;
integer expected_first;

// 记录AXI反压开始时的输出，检查反压期间数据和控制信号保持不变。
reg        stall_check_active;
reg [63:0] stall_tdata;
reg [7:0]  stall_tkeep;
reg        stall_tlast;

trigger_capture dut (
    .clk                 (clk),
    .rst                 (rst),
    .data_in             (data_in),
    .data_in_valid       (data_in_valid),
    .trigger_in          (trigger_in),
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

always #1.6 clk = ~clk;

// 连续产生带序号的IQ数据：I和Q都等于input_index。
always @(posedge clk) begin
    if (rst) begin
        data_in       <= 64'd0;
        data_in_valid <= 1'b0;
        input_index   <= 0;
    end
    else begin
        data_in_valid <= 1'b1;
        data_in       <= {input_index[31:0], input_index[31:0]};
        input_index   <= input_index + 1;
    end
end

// 模拟DMA反压，每7拍暂停接收2拍。
always @(posedge clk) begin
    if (rst) begin
        cycle_count   <= 0;
        m_axis_tready <= 1'b0;
    end
    else begin
        cycle_count <= cycle_count + 1;
        if ((cycle_count % 7) == 3 || (cycle_count % 7) == 4)
            m_axis_tready <= 1'b0;
        else
            m_axis_tready <= 1'b1;
    end
end

// 对AXI输出逐点自检。
always @(posedge clk) begin
    if (rst) begin
        output_index <= 0;
    end
    else if (m_axis_tvalid && m_axis_tready) begin
        if (m_axis_tdata[31:0] !== (expected_first + output_index)) begin
            $error("数据错误：第%0d点，期望%0d，实际%0d",
                   output_index, expected_first + output_index,
                   m_axis_tdata[31:0]);
            error_count = error_count + 1;
        end

        if (m_axis_tdata[63:32] !== m_axis_tdata[31:0]) begin
            $error("I/Q内容不一致：第%0d点", output_index);
            error_count = error_count + 1;
        end

        if (m_axis_tkeep !== 8'hFF) begin
            $error("TKEEP错误：第%0d点，期望FF，实际%02h",
                   output_index, m_axis_tkeep);
            error_count = error_count + 1;
        end

        if (m_axis_tlast !== (output_index == 11)) begin
            $error("TLAST位置错误：第%0d点，tlast=%0b",
                   output_index, m_axis_tlast);
            error_count = error_count + 1;
        end

        output_index <= output_index + 1;
    end
end

// AXI4-Stream反压检查：当TVALID=1且TREADY=0时，输出必须保持稳定。
always @(posedge clk) begin
    if (rst) begin
        stall_check_active <= 1'b0;
        stall_tdata        <= 64'd0;
        stall_tkeep        <= 8'd0;
        stall_tlast        <= 1'b0;
    end
    else begin
        if (stall_check_active) begin
            if (!m_axis_tvalid ||
                (m_axis_tdata !== stall_tdata) ||
                (m_axis_tkeep !== stall_tkeep) ||
                (m_axis_tlast !== stall_tlast)) begin
                $error("AXI反压期间TDATA/TKEEP/TVALID/TLAST没有保持稳定");
                error_count = error_count + 1;
            end
        end

        if (m_axis_tvalid && !m_axis_tready) begin
            stall_check_active <= 1'b1;
            stall_tdata        <= m_axis_tdata;
            stall_tkeep        <= m_axis_tkeep;
            stall_tlast        <= m_axis_tlast;
        end
        else begin
            stall_check_active <= 1'b0;
        end
    end
end

initial begin
    clk                = 1'b0;
    rst                = 1'b1;
    trigger_in         = 1'b0;
    capture_arm        = 1'b0;
    capture_clear      = 1'b0;
    pre_sample_num     = 32'd8;
    post_sample_num    = 32'd4;
    error_count        = 0;
    output_index       = 0;
    expected_first     = 0;
    stall_check_active = 1'b0;
    stall_tdata        = 64'd0;
    stall_tkeep        = 8'd0;
    stall_tlast        = 1'b0;

    repeat (5) @(posedge clk);
    rst <= 1'b0;

    // capture_arm使用上升沿武装。
    repeat (2) @(posedge clk);
    capture_arm <= 1'b1;
    @(posedge clk);
    capture_arm <= 1'b0;

    // 历史数据尚未达到8点时给出触发脉冲，模块应继续停留在ST_PRE，
    // capture_triggered不能置位，也不能产生AXI输出。
    repeat (2) @(negedge clk);
    trigger_in = 1'b1;
    @(negedge clk);
    trigger_in = 1'b0;
    repeat (2) @(posedge clk);

    if (capture_triggered || capture_done || (output_index != 0)) begin
        $error("触发前历史不足时不应接受触发或产生输出");
        error_count = error_count + 1;
    end

    // 等待触发前缓存填满，并继续运行，使8192点环形地址回绕一次。
    wait (capture_ready);
    repeat (8200) @(posedge clk);

    // 记录触发边界。当前拍输入会被归入触发前数据，因此最早输出
    // 应为触发边界之前的8个有效输入点。
    @(negedge clk);
    expected_first = input_index - 8;
    trigger_in = 1'b1;
    @(negedge clk);
    trigger_in = 1'b0;

    wait (capture_done);
    repeat (2) @(posedge clk);

    if (!capture_triggered) begin
        $error("捕获完成后capture_triggered应保持为1");
        error_count = error_count + 1;
    end

    if (captured_sample_num != 32'd12) begin
        $error("捕获长度错误：期望12，实际%0d", captured_sample_num);
        error_count = error_count + 1;
    end

    if (output_index != 12) begin
        $error("AXI输出数量错误：期望12，实际%0d", output_index);
        error_count = error_count + 1;
    end

    if (error_count == 0)
        $display("PASS：触发前后IQ捕获、环形回绕和AXI反压测试通过。");
    else
        $fatal(1, "FAIL：trigger_capture共有%0d个错误。", error_count);

    $finish;
end

endmodule
