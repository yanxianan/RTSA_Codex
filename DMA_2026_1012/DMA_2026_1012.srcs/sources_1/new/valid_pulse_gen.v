// 数据有效位转换模块：将长期为1的valid电平，转换为跟随数据变化节拍的脉冲信号
module valid_pulse_gen #(
    parameter DATA_WIDTH = 64
)(
    input  wire                     clk,       // 输入时钟（如 312.5MHz）
    input  wire                     rst_n,     // 低电平复位
    input  wire                     valid_in,  // 长期保持为 1 的有效电平信号
    input  wire [DATA_WIDTH-1:0]    data_in,   // 输入数据（可能连续多拍保持不变）
    output wire                     valid_out, // 输出脉冲：仅在数据更新时拉高 1 个时钟周期
    output wire [DATA_WIDTH-1:0]    data_out   // 输出数据（与 data_in 对齐）
);

    reg [DATA_WIDTH-1:0] data_d1;   // 寄存上一拍数据

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            data_d1 <= {DATA_WIDTH{1'b0}};
        end else if (valid_in) begin
            // 仅在当前有效信号为高时锁存数据，避免在无效期间误判变化
            data_d1 <= data_in;
        end
    end

    // 当 valid_in 为高，且当前数据与上一拍数据不同时，产生单周期高脉冲
    // 这确保了有效位严格跟随数据的实际更新间隔（如 102.4ns 一次）
    assign valid_out = valid_in && (data_in != data_d1);
    assign data_out  = data_in;

endmodule