module valid_continuous (
    input  wire       clk,          // 时钟信号
    input  wire       rst_n,        // 异步复位，低有效
    input  wire       data_valid_in, // 输入数据有效信号（间断）
    output reg        valid_out      // 输出持续有效信号
);

    // 参数定义：等待超时时间，单位：时钟周期
    // 可根据实际数据间隔调整
    parameter TIMEOUT_COUNT = 16'd1000;  // 建议调整此值

    reg [15:0] timeout_cnt;  // 超时计数器

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            timeout_cnt <= 16'd0;
            valid_out   <= 1'b0;
        end
        else begin
            if (data_valid_in) begin
                // 有数据有效时，重置计数器并保持 valid 拉高
                timeout_cnt <= TIMEOUT_COUNT;
                valid_out   <= 1'b1;
            end
            else if (valid_out) begin
                // 当前无数据有效，但之前 valid 已拉高
                if (timeout_cnt == 16'd0) begin
                    // 计数器归零，表示数据已结束，拉低 valid
                    valid_out <= 1'b0;
                end
                else begin
                    // 计数器递减，valid 继续保持高
                    timeout_cnt <= timeout_cnt - 1'b1;
                end
            end
        end
    end

endmodule