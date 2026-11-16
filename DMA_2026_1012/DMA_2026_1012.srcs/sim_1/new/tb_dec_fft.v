`timescale 1ns / 10ps

module tb_dec_fft;
    // 测试平台信号定义
    reg clk;
    reg rst_n;
    reg signed [13:0] ch0_data;
    reg signed [13:0] ch1_data;
    reg signed [13:0] ch2_data;
    reg signed [13:0] ch3_data;
    reg data_in_valid;
    
    wire signed [31:0] data_time_out;
    wire data_out_time_valid;
    
    wire signed [31:0] data_out_im;
    wire signed [31:0] data_out_re;
    wire data_out_fre_valid;

    // 存储器定义：用于存放从TXT读取的激励数据
    reg signed [13:0] mem [0:31999]; // 14位有符号数，共32000个数据点
    integer addr;                   // 数据读取地址
    
    // 实例化被测模块（DUT）
    dec_fft uut (
        .clk(clk),
        .rst_n(rst_n),
        .ch0_data(ch0_data),
        .ch1_data(ch1_data),
        .ch2_data(ch2_data),
        .ch3_data(ch3_data),
        .data_in_valid(data_in_valid),
        .data_time_out(data_time_out),
        .data_out_time_valid(data_out_time_valid),
        .data_out_re(data_out_re),
        .data_out_im(data_out_im),
        .data_out_fre_valid(data_out_fre_valid)
    );

    // 时钟生成：312.5MHz时钟（周期3.2ns）
    initial begin
        clk = 0;
        forever #1.6 clk = ~clk;  
    end

    // 复位
    initial begin
        rst_n = 0;
        #20;
        rst_n = 1;
    end

    // 读取TXT文件数据
    initial begin
        // 使用$readmemh读取十六进制文件
        $readmemh("D:/Matlab_project/HB_filter/multitone_1.25GHz_14bit_hex.txt", mem, 0, 31999);
        addr = 0;
        // 显示读取状态
        $display("TXT文件读取完成，共32000个数据点");
    end

// 在testbench中实现四通道流水线延迟
reg signed [13:0] ch0_delay1;  // ch0延迟1拍
reg signed [13:0] ch0_delay2;  // ch0延迟2拍
reg signed [13:0] ch0_delay3;  // ch0延迟3拍

initial begin
    // 初始化所有信号
    ch0_data <= 14'd0;
    ch1_data <= 14'd0;
    ch2_data <= 14'd0;
    ch3_data <= 14'd0;
    data_in_valid <= 1'b0;
    ch0_delay1 <= 14'd0;
    ch0_delay2 <= 14'd0;
    ch0_delay3 <= 14'd0;
    
    // 等待复位完成
    @(posedge clk);
    
    // 使用无限循环实现连续写入
    while (1) begin  // 无限循环，仿真将一直运行
        @(posedge clk);
        // 使用取模运算实现地址循环：0 -> 31999 -> 0 -> 31999 ...
        addr = addr % 32000;  // 取模运算，确保地址在0~31999范围内
        
        // 使用非阻塞赋值构建移位寄存器链
        ch0_data     <= mem[addr];        // 原始数据
        ch1_data     <= ch0_data;         // ch0延迟1拍
        ch2_data     <= ch1_data;         // ch1延迟1拍（即ch0延迟2拍）
        ch3_data     <= ch2_data;         // ch2延迟1拍（即ch0延迟3拍）
        data_in_valid <= 1'b1;
        
        addr = addr + 1;  // 地址递增，下一次循环时会被取模
    end
end

  integer file_re_output;
        // 打开输出文件
    initial begin
        file_re_output    =   $fopen("D:/Matlab_project/HB_filter/file_re_output.txt", "w");

        // 检查文件是否成功打开
        if (file_re_output == 0) begin
            $display("错误：无法打开输出文件！请检查路径是否正确");
            $finish;
        end else begin
            $display("输出文件已成功创建");
        end
    end
        always @(posedge clk) begin

        if (data_out_fre_valid) begin
            $fwrite(file_re_output, "%d\n", $signed(data_out_re));
        end
    end
    integer file_im_output;
            // 打开输出文件
    initial begin
        file_im_output    =   $fopen("D:/Matlab_project/HB_filter/file_im_output.txt", "w");
        // 检查文件是否成功打开
        if (file_im_output == 0) begin
            $display("错误：无法打开输出文件！请检查路径是否正确");
            $finish;
        end else begin
            $display("输出文件已成功创建");
        end
    end
        always @(posedge clk) begin

        if (data_out_fre_valid) begin
            $fwrite(file_im_output, "%d\n", $signed(data_out_im));
        end
    end
    

endmodule