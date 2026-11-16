`timescale 1ns / 1ps

// ============================================================================
// 模块名称：CIC_filter
// 功能说明：
//   1. 调用 CIC Compiler IP 完成可变倍率抽取；
//   2. 通过 cic_cfg_valid 接收新的抽取倍率和输出截位位置；
//   3. 将配置请求保持到 CIC IP 的配置通道完成握手；
//   4. 每次改变倍率前复位 CIC 内部状态，避免新旧倍率的数据混在一起；
//   5. 对 CIC 全精度结果先舍入，再输出32位有符号数据。
//
// 当前 CIC_0 的关键配置：
//   - 5级抽取型 CIC，差分延迟为1；
//   - 抽取倍率可编程，范围4～8192；
//   - 输入16位；
//   - 有效输出81位，AXI4-Stream端口按字节补齐为88位。
//
// 使用约束：
//   - cic_cfg_valid 只需要保持一个时钟；模块内部会锁存并保持配置；
//   - 只有 cic_cfg_busy=0 时，外部才可以提交新配置；
//   - 改变倍率会清空CIC状态，重新配置期间输入样本不会被接收；
//   - 上层应尽量在帧边界修改配置，并在 cic_cfg_busy 重新变为0后开始新一帧。
// ============================================================================

module CIC_filter #(
    parameter [15:0] DEFAULT_RATE  = 16'd8,
    parameter [7:0]  DEFAULT_TRUNC = 8'd31
)(
    input  wire                    clk,
    input  wire                    rst_n,

    input  wire signed [15:0]      data_in,
    input  wire                    data_in_valid,

    // PS侧配置接口。
    // cic_rate_cfg 的有效范围为4～8192，数值就是实际抽取倍率。
    input  wire [15:0]             cic_rate_cfg,
    // cic_trunc_cfg 表示32位输出所保留的最高位编号，有效范围为0～80。
    input  wire [7:0]              cic_trunc_cfg,
    // 当 cic_cfg_busy=0 时，用一个时钟高脉冲提交上述两个配置值。
    input  wire                    cic_cfg_valid,
    output wire                    cic_cfg_busy,

    output reg signed [31:0]       data_out,
    output reg                     data_out_valid
);

// ----------------------------------------------------------------------------
// CIC IP接口信号
// ----------------------------------------------------------------------------
wire        [87:0] cic_ip_data;
wire signed [80:0] cic_result_full;
wire               cic_ip_data_valid;
wire               cic_ip_data_ready;
wire               cic_ip_cfg_ready;

reg  [15:0]        pending_rate;
reg  [7:0]         pending_trunc;
reg  [15:0]        active_rate;
reg  [7:0]         active_trunc;
reg  signed [80:0] active_rounding;

reg                cic_ip_aresetn;
reg                cic_ip_cfg_valid;

// ----------------------------------------------------------------------------
// 配置状态机
// ----------------------------------------------------------------------------
localparam [1:0] CFG_RESET = 2'd0;
localparam [1:0] CFG_SEND  = 2'd1;
localparam [1:0] CFG_RUN   = 2'd2;

reg [1:0] cfg_state;
reg [1:0] reset_count;

// CIC IP文档要求 aresetn 至少保持两个时钟周期有效。
localparam [1:0] RESET_HOLD_CYCLES = 2'd2;

// 正常运行时busy为0；复位CIC和发送配置期间busy为1。
assign cic_cfg_busy  = (cfg_state != CFG_RUN);

// CIC IP的AXI端口补齐到88位，但当前IP真正的全精度结果只有bit80～bit0。
// 明确只使用这81位，避免把总线补齐位误当作有效数据参与舍入和截位。
assign cic_result_full = $signed(cic_ip_data[80:0]);

// 只有范围合法的请求才会触发重新配置。
wire cfg_value_legal = (cic_rate_cfg >= 16'd4)    &&
                       (cic_rate_cfg <= 16'd8192) &&
                       (cic_rate_cfg[15:14] == 2'b00) &&
                       (cic_trunc_cfg <= 8'd80);
wire cfg_request_accept = cic_cfg_valid && (cfg_state == CFG_RUN);
wire cfg_change_start   = cfg_request_accept && cfg_value_legal;

// 配置期间禁止数据进入CIC。上层应在帧边界进行配置，避免丢失有效样本。
// 接受新配置的同一拍也禁止送入新样本，防止该样本落入旧倍率的数据流。
wire cic_input_valid = data_in_valid &&
                       (cfg_state == CFG_RUN) &&
                       !cfg_change_start;

// 根据最高保留位生成舍入补偿值。
// 例如保留 data[40:9] 时，截掉的最高位为bit8，因此先加 2^8。
function automatic signed [80:0] make_rounding_value;
    input [7:0] trunc_index;
    begin
        if ((trunc_index >= 8'd32) && (trunc_index <= 8'd80))
            make_rounding_value = 81'sd1 <<< (trunc_index - 8'd32);
        else
            make_rounding_value = 81'sd0;
    end
endfunction

always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
        pending_rate      <= DEFAULT_RATE;
        pending_trunc     <= DEFAULT_TRUNC;
        active_rate       <= DEFAULT_RATE;
        active_trunc      <= DEFAULT_TRUNC;
        active_rounding   <= 81'sd0;
        cic_ip_aresetn    <= 1'b0;
        cic_ip_cfg_valid  <= 1'b0;
        cfg_state         <= CFG_RESET;
        reset_count       <= 2'd0;
    end
    else begin
        case (cfg_state)
            CFG_RESET: begin
                cic_ip_aresetn   <= 1'b0;
                cic_ip_cfg_valid <= 1'b0;

                if (reset_count == (RESET_HOLD_CYCLES - 1'b1)) begin
                    reset_count    <= 2'd0;
                    cic_ip_aresetn <= 1'b1;
                    cfg_state      <= CFG_SEND;
                end
                else begin
                    reset_count <= reset_count + 1'b1;
                end
            end

            CFG_SEND: begin
                cic_ip_aresetn   <= 1'b1;
                cic_ip_cfg_valid <= 1'b1;

                // 只有 VALID 和 READY 同时为1，配置才真正被CIC IP接收。
                if (cic_ip_cfg_valid && cic_ip_cfg_ready) begin
                    cic_ip_cfg_valid <= 1'b0;
                    active_rate      <= pending_rate;
                    active_trunc     <= pending_trunc;
                    active_rounding  <= make_rounding_value(pending_trunc);
                    cfg_state        <= CFG_RUN;
                end
            end

            CFG_RUN: begin
                cic_ip_aresetn   <= 1'b1;
                cic_ip_cfg_valid <= 1'b0;

                if (cfg_request_accept) begin
                    // RATE=8192需要14个有效位，因此这里保留完整16位配置口。
                    if (cfg_value_legal) begin
                        pending_rate     <= cic_rate_cfg;
                        pending_trunc    <= cic_trunc_cfg;
                        cic_ip_aresetn   <= 1'b0;
                        reset_count      <= 2'd0;
                        cfg_state        <= CFG_RESET;
                    end
                    // 非法配置直接忽略，继续使用当前已经生效的配置。
                end
            end

            default: begin
                cic_ip_aresetn   <= 1'b0;
                cic_ip_cfg_valid <= 1'b0;
                reset_count      <= 2'd0;
                cfg_state        <= CFG_RESET;
            end
        endcase
    end
end

// ----------------------------------------------------------------------------
// CIC Compiler IP
// ----------------------------------------------------------------------------
CIC_0 u_cic (
    .aclk                  (clk),
    .aresetn               (cic_ip_aresetn),

    .s_axis_data_tdata     (data_in),
    .s_axis_data_tvalid    (cic_input_valid),
    .s_axis_data_tready    (cic_ip_data_ready),

    // RATE字段从TDATA最低位开始存放，高位为填充位。
    .s_axis_config_tdata   ({2'b00, pending_rate[13:0]}),
    .s_axis_config_tvalid  (cic_ip_cfg_valid),
    .s_axis_config_tready  (cic_ip_cfg_ready),

    .m_axis_data_tdata     (cic_ip_data),
    .m_axis_data_tvalid    (cic_ip_data_valid)
);

// ----------------------------------------------------------------------------
// CIC输出舍入与32位截取
//
// 第1拍：完成81位舍入加法，并锁存本样本对应的截位位置。
// 第2拍：根据锁存的截位位置输出32位结果。
// valid与数据经过完全相同的两级流水，保证不会错位。
// ----------------------------------------------------------------------------
reg signed [80:0] rounded_data_s1;
reg        [7:0]  trunc_index_s1;
reg               output_valid_s1;

always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
        rounded_data_s1 <= 81'sd0;
        trunc_index_s1  <= 8'd0;
        output_valid_s1 <= 1'b0;
        data_out         <= 32'sd0;
        data_out_valid   <= 1'b0;
    end
    else if ((cfg_state != CFG_RUN) || cfg_change_start) begin
        // 配置切换时丢弃旧倍率残留结果，避免污染新一帧。
        rounded_data_s1 <= 81'sd0;
        trunc_index_s1  <= 8'd0;
        output_valid_s1 <= 1'b0;
        data_out         <= 32'sd0;
        data_out_valid   <= 1'b0;
    end
    else begin
        output_valid_s1 <= cic_ip_data_valid;
        data_out_valid  <= output_valid_s1;

        if (cic_ip_data_valid) begin
            rounded_data_s1 <= cic_result_full + active_rounding;
            trunc_index_s1  <= active_trunc;
        end

        if (output_valid_s1) begin
            if (trunc_index_s1 < 8'd31) begin
                // 有效数据不足32位时左移，使最高有效位对齐到bit31。
                data_out <= rounded_data_s1[31:0]
                            <<< (8'd31 - trunc_index_s1);
            end
            else begin
                // trunc_index_s1为输出最高位，向下连续保留32位。
                data_out <= rounded_data_s1[trunc_index_s1 -: 32];
            end
        end
    end
end

// active_rate保留为内部状态，便于ILA观察当前已经生效的抽取倍率。
// cic_ip_data_ready通常为1；如果它拉低，上游当前没有反压接口，因此上层仍需
// 保证输入速率不超过CIC IP的处理能力。

endmodule
