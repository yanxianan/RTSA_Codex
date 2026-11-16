`timescale 1ns / 1ps

//==============================================================================
// FFT逐频点最大保持
//
// 每个频点按实部平方加虚部平方比较，保存最大功率对应的完整复数数据。
// 累计hold_frames_cfg帧后输出一帧，输出格式仍为低32位实部、高32位虚部。
// 配置值在每组第一点锁存，写0按1帧处理。
//==============================================================================
module fft_max_hold #(
    parameter integer FFT_POINTS = 1024
)(
    input  wire         clk,
    input  wire         rst,

    input  wire [63:0]  s_axis_tdata,
    input  wire         s_axis_tvalid,
    output wire         s_axis_tready,
    input  wire         s_axis_tlast,

    input  wire [15:0]  hold_frames_cfg,

    output wire [63:0]  m_axis_tdata,
    output reg          m_axis_tvalid,
    input  wire         m_axis_tready,
    output reg          m_axis_tlast
);

localparam integer BIN_WIDTH = (FFT_POINTS <= 2) ? 1 : $clog2(FFT_POINTS);

(* ram_style = "block" *) reg [63:0] max_power_mem [0:FFT_POINTS-1];
(* ram_style = "block" *) reg [63:0] max_data_mem  [0:FFT_POINTS-1];

reg [BIN_WIDTH-1:0]   input_bin;
reg [15:0]            input_frame;
reg [15:0]            active_hold_frames;
reg                   input_block;
reg                   output_active;
reg [BIN_WIDTH-1:0]   output_bin;
reg [63:0]            max_data_dout;

wire input_transfer = s_axis_tvalid && s_axis_tready;
wire input_frame_end = s_axis_tlast || (input_bin == FFT_POINTS - 1);
wire input_first_frame = (input_frame == 0);
wire input_group_start = (input_frame == 0) && (input_bin == 0);
wire [15:0] requested_hold_frames = (hold_frames_cfg == 0) ?
                                    16'd1 : hold_frames_cfg;
wire [15:0] current_hold_frames = input_group_start ?
                                  requested_hold_frames :
                                  active_hold_frames;
wire input_last_frame = (input_frame == current_hold_frames - 1'b1);

assign s_axis_tready = !rst && !input_block && !output_active;
assign m_axis_tdata = max_data_dout;

// 第1级：锁存复数数据、频点和当前最大功率。
reg signed [31:0] real_s1;
reg signed [31:0] imag_s1;
reg        [63:0] data_s1;
reg        [63:0] old_power_s1;
reg [BIN_WIDTH-1:0] bin_s1;
reg first_s1;
reg group_end_s1;
reg valid_s1;

always @(posedge clk) begin
    if (rst) begin
        real_s1      <= 32'sd0;
        imag_s1      <= 32'sd0;
        data_s1      <= 64'd0;
        old_power_s1 <= 64'd0;
        bin_s1       <= {BIN_WIDTH{1'b0}};
        first_s1     <= 1'b0;
        group_end_s1 <= 1'b0;
        valid_s1     <= 1'b0;
    end
    else begin
        valid_s1 <= input_transfer;
        if (input_transfer) begin
            real_s1      <= $signed(s_axis_tdata[31:0]);
            imag_s1      <= $signed(s_axis_tdata[63:32]);
            data_s1      <= s_axis_tdata;
            old_power_s1 <= max_power_mem[input_bin];
            bin_s1       <= input_bin;
            first_s1     <= input_first_frame;
            group_end_s1 <= input_frame_end && input_last_frame;
        end
    end
end

// 第2级：计算实部平方和虚部平方。
(* use_dsp = "yes" *) reg [63:0] real_square_s2;
(* use_dsp = "yes" *) reg [63:0] imag_square_s2;
reg [63:0] data_s2;
reg [63:0] old_power_s2;
reg [BIN_WIDTH-1:0] bin_s2;
reg first_s2;
reg group_end_s2;
reg valid_s2;

always @(posedge clk) begin
    if (rst) begin
        real_square_s2 <= 64'd0;
        imag_square_s2 <= 64'd0;
        data_s2        <= 64'd0;
        old_power_s2   <= 64'd0;
        bin_s2         <= {BIN_WIDTH{1'b0}};
        first_s2       <= 1'b0;
        group_end_s2   <= 1'b0;
        valid_s2       <= 1'b0;
    end
    else begin
        valid_s2 <= valid_s1;
        if (valid_s1) begin
            real_square_s2 <= real_s1 * real_s1;
            imag_square_s2 <= imag_s1 * imag_s1;
            data_s2        <= data_s1;
            old_power_s2   <= old_power_s1;
            bin_s2         <= bin_s1;
            first_s2       <= first_s1;
            group_end_s2   <= group_end_s1;
        end
    end
end

// 第3级：形成线性功率并保持控制信息对齐。
reg [63:0] power_s3;
reg [63:0] data_s3;
reg [63:0] old_power_s3;
reg [BIN_WIDTH-1:0] bin_s3;
reg first_s3;
reg group_end_s3;
reg valid_s3;

wire update_max = !rst && valid_s3 &&
                  (first_s3 || (power_s3 > old_power_s3));
wire output_start = valid_s3 && group_end_s3;
wire output_transfer = output_active && m_axis_tvalid && m_axis_tready;
wire output_next = output_transfer && (output_bin != FFT_POINTS - 1);
wire data_read_en = output_start || output_next;
wire [BIN_WIDTH-1:0] data_read_addr = output_start ?
                                       {BIN_WIDTH{1'b0}} :
                                       output_bin + 1'b1;

always @(posedge clk) begin
    if (rst) begin
        power_s3     <= 64'd0;
        data_s3      <= 64'd0;
        old_power_s3 <= 64'd0;
        bin_s3       <= {BIN_WIDTH{1'b0}};
        first_s3     <= 1'b0;
        group_end_s3 <= 1'b0;
        valid_s3     <= 1'b0;
    end
    else begin
        valid_s3 <= valid_s2;
        if (valid_s2) begin
            power_s3     <= real_square_s2 + imag_square_s2;
            data_s3      <= data_s2;
            old_power_s3 <= old_power_s2;
            bin_s3       <= bin_s2;
            first_s3     <= first_s2;
            group_end_s3 <= group_end_s2;
        end
    end
end

// 第一帧直接覆盖旧存储，后续帧只在功率更大时更新。
always @(posedge clk) begin
    if (update_max)
        max_power_mem[bin_s3] <= power_s3;
end

// 使用标准单写单读模板，使数据存储映射到块 RAM。
always @(posedge clk) begin
    if (update_max)
        max_data_mem[bin_s3]  <= data_s3;

    if (data_read_en)
        max_data_dout <= max_data_mem[data_read_addr];
end

// 输入计数以及输出AXI流控制。
always @(posedge clk) begin
    if (rst) begin
        input_bin    <= {BIN_WIDTH{1'b0}};
        input_frame  <= 16'd0;
        active_hold_frames <= 16'd1;
        input_block  <= 1'b0;
        output_active <= 1'b0;
        output_bin   <= {BIN_WIDTH{1'b0}};
        m_axis_tvalid <= 1'b0;
        m_axis_tlast <= 1'b0;
    end
    else begin
        if (input_transfer) begin
            if (input_group_start)
                active_hold_frames <= requested_hold_frames;

            if (input_frame_end) begin
                input_bin <= {BIN_WIDTH{1'b0}};
                if (input_last_frame) begin
                    input_block <= 1'b1;
                end
                else begin
                    input_frame <= input_frame + 1'b1;
                end
            end
            else begin
                input_bin <= input_bin + 1'b1;
            end
        end

        // 最后一帧的最后一个频点完成比较后，开始输出最大保持帧。
        if (output_start) begin
            output_active <= 1'b1;
            output_bin    <= {BIN_WIDTH{1'b0}};
            m_axis_tvalid <= 1'b1;
            m_axis_tlast  <= (FFT_POINTS == 1);
        end
        else if (output_transfer) begin
            if (output_bin == FFT_POINTS - 1) begin
                output_active <= 1'b0;
                input_block   <= 1'b0;
                input_frame   <= 16'd0;
                m_axis_tvalid <= 1'b0;
                m_axis_tlast  <= 1'b0;
            end
            else begin
                output_bin    <= output_bin + 1'b1;
                m_axis_tlast  <= (output_bin == FFT_POINTS - 2);
            end
        end
    end
end

endmodule
