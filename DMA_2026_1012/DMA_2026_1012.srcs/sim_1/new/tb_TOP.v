`timescale 1ns / 1ps

module tb_TOP();

    // 时钟与复位信号
    reg  adc_clk_p;
    wire adc_clk_n;
    reg  rst_n;
    
    wire [31:0] data_out_time_i;
    wire [31:0] data_out_time_q;
    wire data_out_time_valid;

    wire [63:0] data_out_fre;
    wire data_out_fre_valid;

    // 生成差分时钟：100MHz 
    initial begin
        adc_clk_p = 0;
        forever #5 adc_clk_p = ~adc_clk_p;  
    end

    // 差分时钟负端直接取反
    assign adc_clk_n = ~adc_clk_p;

    // 复位信号：低有效，初始为低，100ns 后释放
    initial begin
        rst_n = 0;
        #100;
        rst_n = 1;
        // 运行足够长时间（例如 10μs）后结束仿真

    end

    // 例化待测顶层模块
    TOP uut (
        .adc_clk_p (adc_clk_p),
        .adc_clk_n (adc_clk_n),
        .rst_n     (rst_n),
        .data_out_time_i(data_out_time_i),
        .data_out_time_q(data_out_time_q),
        .data_out_time_valid(data_out_time_valid),
        .data_out_fre(data_out_fre),
        .data_out_fre_valid(data_out_fre_valid)
    );
    reg clk;
    // 时钟生成：312.5MHz时钟（周期3.2ns）
    initial begin
        clk = 0;
        forever #1.6 clk = ~clk;  
    end
  integer file_output_iq;
        // 打开输出文件
    initial begin
        file_output_iq    =   $fopen("D:/Matlab_project/HB_filter/file_output_iq.txt", "w");

        // 检查文件是否成功打开
        if (file_output_iq == 0) begin
            $display("错误：无法打开输出文件！请检查路径是否正确");
            $finish;
        end else begin
            $display("输出文件已成功创建");
        end
    end
always @(posedge clk) begin
    if (data_out_fre_valid) begin
        // 将 64 位数据拆分为虚部（高 32 位）和实部（低 32 位）
        $fwrite(file_output_iq, "%d %d\n", 
                $signed(data_out_fre[63:32]),  // 虚部（Q路）
                $signed(data_out_fre[31:0]));  // 实部（I路）
    end
end
endmodule