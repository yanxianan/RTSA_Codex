`timescale 1ns / 1ps

// ============================================================================
// window_rom自校验测试平台
//
// 验证内容：
//   1. 验证I/Q采样点与1024点Blackman-Harris窗系数逐点对应；
//   2. 验证正数、负数以及不同幅度输入的乘法和Q1.17缩放结果；
//   3. 在帧内插入data_in_valid空拍，验证空拍不会引起窗系数错位；
//   4. 连续发送两帧，验证窗系数地址在1024点后正确回到0；
//   5. 验证每帧恰好输出1024个有效点，并且last只出现在第1024点；
//   6. 验证I/Q数据顺序以及输出总数量均正确。
//
// 使用方法：
//   1. 将本文件添加到Vivado的Simulation Sources；
//   2. 将tb_window_rom_modified设置为仿真顶层；
//   3. 确保以下源文件或IP已经加入工程：
//        window_rom.v
//        rom_blackman_harris_1024
//        mult_window_32s_18u
//   4. 运行Run All，控制台最终应显示TEST PASSED。
//
// 建议加入波形窗口的信号：
//   clk、rst、data_in、data_in_valid
//   dut.window_addr、dut.data_s1、dut.data_s2、dut.window_coeff
//   dut.valid_s2、dut.mult_valid_pipe
//   dut.product_i_mult、dut.product_q_mult
//   data_out、data_out_valid、data_out_last
//   accepted_count、received_count、error_count
// ============================================================================

module tb_window_rom_modified;

localparam integer FRAME_LENGTH  = 1024;
localparam integer FRAME_COUNT   = 2;
localparam integer TOTAL_SAMPLES = FRAME_LENGTH * FRAME_COUNT;
localparam real    CLK_PERIOD_NS = 3.2;  // 312.5 MHz

reg         clk;
reg         rst;
reg [63:0]  data_in;
reg         data_in_valid;

wire [63:0] data_out;
wire        data_out_valid;
wire        data_out_last;

// 将I/Q拆开显示，便于在波形窗口中直接观察有符号十进制数。
wire signed [31:0] data_out_i;
wire signed [31:0] data_out_q;

assign data_out_i = data_out[31:0];
assign data_out_q = data_out[63:32];

// 测试状态计数器保留为顶层变量，便于直接加入波形窗口。
integer accepted_count;
integer received_count;
integer error_count;
integer last_count;
integer timeout_count;

// 当前输入样本信息，主要用于波形观察。
integer             drive_frame_index;
integer             drive_point_index;
reg signed [31:0]   drive_i;
reg signed [31:0]   drive_q;

// 当前正在检查的期望值，主要用于波形观察。
reg signed [31:0] expected_i_now;
reg signed [31:0] expected_q_now;
reg               expected_last_now;

// 每个输入点的参考结果按照输入顺序保存，输出时再按顺序逐点比较。
reg signed [31:0] expected_i    [0:TOTAL_SAMPLES-1];
reg signed [31:0] expected_q    [0:TOTAL_SAMPLES-1];
reg               expected_last [0:TOTAL_SAMPLES-1];

// 312.5 MHz时钟。
initial clk = 1'b0;
always #(CLK_PERIOD_NS/2.0) clk = ~clk;

// 待测加窗模块。
window_rom dut (
    .clk            (clk),
    .rst            (rst),
    .data_in        (data_in),
    .data_in_valid  (data_in_valid),
    .data_out       (data_out),
    .data_out_valid (data_out_valid),
    .data_out_last  (data_out_last)
);

// --------------------------------------------------------------------------
// 根据公式独立计算指定地址的四项Blackman-Harris窗系数。
// 量化规则与MATLAB生成COE文件时保持一致：乘以2^17后四舍五入，
// 并限制在18位无符号数能够表示的范围内。
// --------------------------------------------------------------------------
function automatic [17:0] coefficient_for_address;
    input integer address;
    real phase;
    real coefficient;
    integer quantized_value;
    begin
        phase = 2.0 * 3.14159265358979323846 * address /
                (FRAME_LENGTH - 1.0);

        coefficient = 0.35875
                    - 0.48829 * $cos(phase)
                    + 0.14128 * $cos(2.0 * phase)
                    - 0.01168 * $cos(3.0 * phase);

        quantized_value = $rtoi(coefficient * 131072.0 + 0.5);

        if (quantized_value < 0)
            quantized_value = 0;
        else if (quantized_value > 131071)
            quantized_value = 131071;

        coefficient_for_address = quantized_value[17:0];
    end
endfunction

// --------------------------------------------------------------------------
// 参考模型使用19位有符号数保存18位无符号窗系数。
// 最高位补0可以避免系数被误认为负数，32位数据乘19位系数得到51位乘积。
// --------------------------------------------------------------------------
function automatic signed [50:0] reference_multiply;
    input signed [31:0] sample_value;
    input        [17:0] coefficient_value;
    reg signed  [18:0] coefficient_signed;
    begin
        coefficient_signed = {1'b0, coefficient_value};
        reference_multiply = sample_value * coefficient_signed;
    end
endfunction

// --------------------------------------------------------------------------
// 对Q1.17乘积执行与待测模块相同的有符号对称四舍五入。
// --------------------------------------------------------------------------
function automatic signed [31:0] round_q17_reference;
    input signed [50:0] product;
    reg signed [51:0] product_ext;
    reg signed [51:0] magnitude;
    reg signed [51:0] rounded_value;
    begin
        product_ext = {product[50], product};

        if (product_ext >= 0) begin
            rounded_value = (product_ext + 52'sd65536) >>> 17;
        end
        else begin
            magnitude     = -product_ext;
            rounded_value = -((magnitude + 52'sd65536) >>> 17);
        end

        round_q17_reference = rounded_value[31:0];
    end
endfunction

// --------------------------------------------------------------------------
// 生成I路测试样本。
// 两帧使用不同规律，并在窗函数中心附近加入较大幅度样本，便于检查
// 乘法符号、完整精度和四舍五入逻辑。
// --------------------------------------------------------------------------
function automatic signed [31:0] make_i_sample;
    input integer frame_number;
    input integer point_number;
    integer value;
    begin
        if (point_number == 511)
            value = (frame_number == 0) ? 123456789 : -135791113;
        else if (point_number == 512)
            value = (frame_number == 0) ? -98765432 : 246813579;
        else if (frame_number == 0)
            value = point_number * 977 - 500000;
        else
            value = 700000 - point_number * 1231;

        make_i_sample = value;
    end
endfunction

// --------------------------------------------------------------------------
// 生成Q路测试样本，变化规律与I路不同，用于检查I/Q是否发生交换或串扰。
// --------------------------------------------------------------------------
function automatic signed [31:0] make_q_sample;
    input integer frame_number;
    input integer point_number;
    integer value;
    begin
        if (point_number == 511)
            value = (frame_number == 0) ? -76543210 : 112233445;
        else if (point_number == 512)
            value = (frame_number == 0) ? 87654321 : -223344556;
        else if (frame_number == 0)
            value = 350000 - point_number * 761;
        else
            value = point_number * 1459 - 750000;

        make_q_sample = value;
    end
endfunction

// --------------------------------------------------------------------------
// 插入指定数量的无效时钟。
// 输入在下降沿改变，保证在下一个上升沿到来前已经稳定，并避免TB与DUT
// 在同一上升沿修改、采样信号而产生仿真竞争。
// --------------------------------------------------------------------------
task automatic drive_invalid_cycles;
    input integer cycle_count;
    integer cycle_index;
    begin
        for (cycle_index = 0;
             cycle_index < cycle_count;
             cycle_index = cycle_index + 1) begin
            @(negedge clk);
            data_in_valid = 1'b0;
            data_in       = 64'd0;
        end
    end
endtask

// --------------------------------------------------------------------------
// 发送一个有效复数采样点，同时计算并保存该点的参考结果。
// --------------------------------------------------------------------------
task automatic drive_one_sample;
    input integer frame_number;
    input integer point_number;
    reg [17:0] coefficient_value;
    reg signed [50:0] product_i_reference;
    reg signed [50:0] product_q_reference;
    begin
        drive_frame_index = frame_number;
        drive_point_index = point_number;
        drive_i = make_i_sample(frame_number, point_number);
        drive_q = make_q_sample(frame_number, point_number);

        coefficient_value  = coefficient_for_address(point_number);
        product_i_reference = reference_multiply(drive_i, coefficient_value);
        product_q_reference = reference_multiply(drive_q, coefficient_value);

        expected_i[accepted_count] =
            round_q17_reference(product_i_reference);
        expected_q[accepted_count] =
            round_q17_reference(product_q_reference);
        expected_last[accepted_count] =
            (point_number == FRAME_LENGTH - 1);

        @(negedge clk);
        data_in       = {drive_q, drive_i};
        data_in_valid = 1'b1;
        accepted_count = accepted_count + 1;
    end
endtask

// --------------------------------------------------------------------------
// 发送完整一帧数据。
// 每37个有效点插入一个空拍，用来验证地址只随有效采样点递增。
// --------------------------------------------------------------------------
task automatic drive_one_frame;
    input integer frame_number;
    integer point_number;
    begin
        for (point_number = 0;
             point_number < FRAME_LENGTH;
             point_number = point_number + 1) begin
            if ((point_number != 0) && ((point_number % 37) == 0))
                drive_invalid_cycles(1);

            drive_one_sample(frame_number, point_number);
        end
    end
endtask

// --------------------------------------------------------------------------
// 输出监视与自动比较。
// 在下降沿检查是为了让DUT在上升沿更新的寄存器和IP输出充分稳定；
// 这只是测试平台的采样方式，不代表实际设计使用下降沿逻辑。
// --------------------------------------------------------------------------
always @(negedge clk) begin
    if (!rst) begin
        if (data_out_last && !data_out_valid) begin
            $error("data_out_last asserted while data_out_valid is low");
            error_count = error_count + 1;
        end

        if (data_out_valid) begin
            if (received_count >= accepted_count) begin
                $error("output arrived without a matching accepted input");
                error_count = error_count + 1;
            end
            else begin
                expected_i_now    = expected_i[received_count];
                expected_q_now    = expected_q[received_count];
                expected_last_now = expected_last[received_count];

                if (data_out_i !== expected_i_now) begin
                    $error("I mismatch: sample=%0d expected=%0d actual=%0d",
                           received_count, expected_i_now, data_out_i);
                    error_count = error_count + 1;
                end

                if (data_out_q !== expected_q_now) begin
                    $error("Q mismatch: sample=%0d expected=%0d actual=%0d",
                           received_count, expected_q_now, data_out_q);
                    error_count = error_count + 1;
                end

                if (data_out_last !== expected_last_now) begin
                    $error("last mismatch: sample=%0d expected=%0b actual=%0b",
                           received_count,
                           expected_last_now,
                           data_out_last);
                    error_count = error_count + 1;
                end

                if (data_out_last)
                    last_count = last_count + 1;
            end

            received_count = received_count + 1;
        end
    end
end

// --------------------------------------------------------------------------
// 主测试流程。
// --------------------------------------------------------------------------
initial begin
    rst                 = 1'b1;
    data_in             = 64'd0;
    data_in_valid       = 1'b0;
    accepted_count      = 0;
    received_count      = 0;
    error_count         = 0;
    last_count          = 0;
    timeout_count       = 0;
    drive_frame_index   = 0;
    drive_point_index   = 0;
    drive_i             = 32'sd0;
    drive_q             = 32'sd0;
    expected_i_now      = 32'sd0;
    expected_q_now      = 32'sd0;
    expected_last_now   = 1'b0;

    // 保持复位若干拍，清空地址和所有valid/last流水线。
    repeat (8) @(negedge clk);
    rst = 1'b0;

    // 复位释放后留出两个空闲周期，再发送第一帧。
    drive_invalid_cycles(2);
    drive_one_frame(0);

    // 两帧之间插入若干空闲周期，验证地址回绕后仍从系数0开始。
    drive_invalid_cycles(9);
    drive_one_frame(1);

    // 停止输入并等待最后一个结果通过完整流水线。
    drive_invalid_cycles(1);

    while ((received_count < TOTAL_SAMPLES) &&
           (timeout_count < 100)) begin
        @(negedge clk);
        timeout_count = timeout_count + 1;
    end

    // 再等待一个下降沿，避免最终计数和主流程在同一仿真时刻竞争。
    @(negedge clk);

    if (accepted_count != TOTAL_SAMPLES) begin
        $error("accepted sample count mismatch: expected=%0d actual=%0d",
               TOTAL_SAMPLES, accepted_count);
        error_count = error_count + 1;
    end

    if (received_count != TOTAL_SAMPLES) begin
        $error("received sample count mismatch: expected=%0d actual=%0d",
               TOTAL_SAMPLES, received_count);
        error_count = error_count + 1;
    end

    if (last_count != FRAME_COUNT) begin
        $error("last pulse count mismatch: expected=%0d actual=%0d",
               FRAME_COUNT, last_count);
        error_count = error_count + 1;
    end

    if (error_count == 0) begin
        $display("============================================================");
        $display("TEST PASSED");
        $display("Accepted samples : %0d", accepted_count);
        $display("Received samples : %0d", received_count);
        $display("Last pulses      : %0d", last_count);
        $display("============================================================");
    end
    else begin
        $fatal(1, "TEST FAILED: error_count=%0d", error_count);
    end

    $finish;
end

// 防止接口异常导致仿真无限运行。
initial begin
    #(CLK_PERIOD_NS * 10000.0);
    $fatal(1, "TEST TIMEOUT");
end

endmodule
