`timescale 1ns / 1ps

//==============================================================================
// 触发前后时域IQ捕获模块
//
// 功能：
//   1. 武装后，将输入的64位时域IQ连续写入环形RAM；
//   2. RAM始终保留最近的触发前数据；
//   3. 收到trigger_in后，再保存指定数量的触发后数据；
//   4. 捕获结束后，按“触发前数据、触发后数据”的时间顺序，通过
//      AXI4-Stream接口输出给DMA；
//   5. AXI4-Stream被反压时，TDATA、TVALID和TLAST保持不变。
//
// 数据格式：
//   data_in[31:0]  ：32位有符号I数据
//   data_in[63:32] ：32位有符号Q数据
//
// 使用限制：
//   1. 本模块固定配合trigger_capture_ram_8192x64使用；
//   2. pre_sample_num + post_sample_num不能超过8192；
//   3. capture_arm按上升沿识别，软件应先写0，再写1完成一次武装；
//   4. trigger_in到达时，本拍输入数据被归入触发前数据；之后接收的
//      post_sample_num个有效数据属于触发后数据。
//==============================================================================
module trigger_capture (
    input  wire                      clk,
    input  wire                      rst,

    // 抽取滤波后的时域IQ数据。
    input  wire [63:0]               data_in,
    input  wire                      data_in_valid,

    // 频率模板触发模块输出的单周期触发脉冲。
    input  wire                      trigger_in,

    // 捕获控制与长度配置，单位均为“有效IQ采样点”。
    input  wire                      capture_arm,
    input  wire                      capture_clear,
    input  wire [31:0]               pre_sample_num,
    input  wire [31:0]               post_sample_num,

    // AXI4-Stream输出，直接连接DMA的S_AXIS_S2MM接口。
    output reg  [63:0]               m_axis_tdata,
    output wire [7:0]                m_axis_tkeep,
    output reg                       m_axis_tvalid,
    input  wire                      m_axis_tready,
    output reg                       m_axis_tlast,

    // 状态输出，便于PS读取或ILA观察。
    output wire                      capture_busy,
    output wire                      capture_ready,
    output reg                       capture_triggered,
    output reg                       capture_done,
    output reg  [31:0]               captured_sample_num
);

localparam integer ADDR_WIDTH = 13;
localparam integer MEM_DEPTH  = 8192;

//------------------------------------------------------------------------------
// 状态机定义
//------------------------------------------------------------------------------
localparam [2:0] ST_IDLE = 3'd0;
localparam [2:0] ST_PRE  = 3'd1;
localparam [2:0] ST_POST = 3'd2;
localparam [2:0] ST_SEND = 3'd3;
localparam [2:0] ST_DONE = 3'd4;

reg [2:0] state;

// write_ptr始终指向“下一个将要写入”的RAM地址。
reg [ADDR_WIDTH-1:0] write_ptr;

// 捕获数据的第一个地址，以及发送阶段当前读取的地址。
reg [ADDR_WIDTH-1:0] capture_start_ptr;
reg [ADDR_WIDTH-1:0] read_ptr;

// 配置值在武装时锁存，防止捕获过程中软件修改配置造成错乱。
reg [31:0] pre_count_cfg;
reg [31:0] post_count_cfg;
reg [31:0] total_count_cfg;

// history_count最多计到MEM_DEPTH，表示RAM中已有多少个有效历史点。
reg [31:0] history_count;
reg [31:0] post_count;
reg [31:0] send_remaining;

// Port B读取延迟为1拍，因此使用pending寄存器同步数据和TLAST。
reg ram_read_pending;
reg ram_read_last_pending;

assign capture_busy = (state == ST_PRE)  ||
                      (state == ST_POST) ||
                      (state == ST_SEND);

// 当前已经保留足够数量的触发前数据时，才允许接受触发。
assign capture_ready = (state == ST_PRE) &&
                       (history_count >= pre_count_cfg);

// 每个输出数据的所有字节都有效。
assign m_axis_tkeep = 8'hFF;

// 利用地址自然溢出实现环形回绕；因此MEM_DEPTH必须是2的整数次幂。
wire [ADDR_WIDTH-1:0] write_ptr_next = write_ptr + 1'b1;

// 如果本拍有有效输入，触发边界位于本拍数据之后。
wire [ADDR_WIDTH-1:0] trigger_boundary_ptr =
                         data_in_valid ? write_ptr_next : write_ptr;

// 判断包含本拍输入后，触发前历史数据是否已经填满。
wire [32:0] history_count_next =
                         {1'b0, history_count} +
                         ((data_in_valid && (history_count < MEM_DEPTH)) ?
                          33'd1 : 33'd0);

wire pre_history_ready_next = (history_count_next >=
                               {1'b0, pre_count_cfg});

// 配置总长度使用33位计算，避免两个32位数相加溢出。
wire [32:0] requested_total = {1'b0, pre_sample_num} +
                              {1'b0, post_sample_num};

wire requested_config_valid =
        (requested_total != 33'd0) &&
        (requested_total <= MEM_DEPTH) &&
        (pre_sample_num  <= MEM_DEPTH) &&
        (post_sample_num <= MEM_DEPTH);

//------------------------------------------------------------------------------
// 8192×64位Simple Dual Port RAM IP
//------------------------------------------------------------------------------
// Port A只写入，Port B只读取；两个时钟端口连接同一个clk。
wire ram_write_enable = data_in_valid &&
                        ((state == ST_PRE) || (state == ST_POST));

// 只有当前没有等待中的RAM数据，且AXI输出寄存器可以腾出位置时，
// 才发起下一次读操作。该方式允许TVALID中存在空拍，但能可靠处理反压。
wire ram_read_enable = (state == ST_SEND) &&
                       !ram_read_pending &&
                       (!m_axis_tvalid || m_axis_tready) &&
                       (send_remaining != 32'd0);

wire [63:0] ram_read_data;

trigger_capture_ram_8192x64 u_trigger_capture_ram (
    .clka  (clk),
    .ena   (ram_write_enable),
    .wea   (ram_write_enable),
    .addra (write_ptr),
    .dina  (data_in),
    .clkb  (clk),
    .enb   (ram_read_enable),
    .addrb (read_ptr),
    .doutb (ram_read_data)
);

//------------------------------------------------------------------------------
// capture_arm上升沿检测
//------------------------------------------------------------------------------
reg capture_arm_d;
wire capture_arm_pulse = capture_arm && !capture_arm_d;

always @(posedge clk) begin
    if (rst)
        capture_arm_d <= 1'b0;
    else
        capture_arm_d <= capture_arm;
end

//------------------------------------------------------------------------------
// 捕获状态机
//------------------------------------------------------------------------------
always @(posedge clk) begin
    if (rst) begin
        state               <= ST_IDLE;
        write_ptr           <= {ADDR_WIDTH{1'b0}};
        capture_start_ptr   <= {ADDR_WIDTH{1'b0}};
        read_ptr            <= {ADDR_WIDTH{1'b0}};
        pre_count_cfg       <= 32'd0;
        post_count_cfg      <= 32'd0;
        total_count_cfg     <= 32'd0;
        history_count       <= 32'd0;
        post_count          <= 32'd0;
        send_remaining      <= 32'd0;
        ram_read_pending    <= 1'b0;
        ram_read_last_pending <= 1'b0;
        capture_triggered   <= 1'b0;
        capture_done        <= 1'b0;
        captured_sample_num <= 32'd0;
        m_axis_tdata        <= 64'd0;
        m_axis_tvalid       <= 1'b0;
        m_axis_tlast        <= 1'b0;
    end
    else if (capture_clear) begin
        // 清除后回到空闲状态，RAM内容不必清零，会在下次捕获时覆盖。
        state               <= ST_IDLE;
        write_ptr           <= {ADDR_WIDTH{1'b0}};
        capture_start_ptr   <= {ADDR_WIDTH{1'b0}};
        read_ptr            <= {ADDR_WIDTH{1'b0}};
        history_count       <= 32'd0;
        post_count          <= 32'd0;
        send_remaining      <= 32'd0;
        ram_read_pending    <= 1'b0;
        ram_read_last_pending <= 1'b0;
        capture_triggered   <= 1'b0;
        capture_done        <= 1'b0;
        captured_sample_num <= 32'd0;
        m_axis_tvalid       <= 1'b0;
        m_axis_tlast        <= 1'b0;
    end
    else begin
        case (state)
            ST_IDLE: begin
                m_axis_tvalid <= 1'b0;
                m_axis_tlast  <= 1'b0;
                ram_read_pending <= 1'b0;

                // 配置有效时才响应武装；配置无效时保持空闲。
                if (capture_arm_pulse && requested_config_valid) begin
                    capture_done        <= 1'b0;
                    capture_triggered   <= 1'b0;
                    captured_sample_num <= 32'd0;
                    pre_count_cfg       <= pre_sample_num;
                    post_count_cfg      <= post_sample_num;
                    total_count_cfg     <= requested_total[31:0];
                    write_ptr           <= {ADDR_WIDTH{1'b0}};
                    history_count       <= 32'd0;
                    post_count          <= 32'd0;
                    state               <= ST_PRE;
                end
            end

            ST_PRE: begin
                // 未触发时持续循环写入，RAM中始终保留最近的历史数据。
                if (data_in_valid) begin
                    write_ptr <= write_ptr_next;

                    if (history_count < MEM_DEPTH)
                        history_count <= history_count + 1'b1;
                end

                // 只有历史数据达到配置长度后才响应触发。
                if (trigger_in && pre_history_ready_next) begin
                    capture_triggered <= 1'b1;

                    // trigger_boundary_ptr之前的pre_count_cfg个点属于
                    // 触发前数据；截取低ADDR_WIDTH位即可完成环形回绕。
                    capture_start_ptr <= trigger_boundary_ptr -
                                         pre_count_cfg[ADDR_WIDTH-1:0];
                    post_count <= 32'd0;

                    if (post_count_cfg == 32'd0) begin
                        read_ptr       <= trigger_boundary_ptr -
                                          pre_count_cfg[ADDR_WIDTH-1:0];
                        send_remaining <= total_count_cfg;
                        state          <= ST_SEND;
                    end
                    else begin
                        state <= ST_POST;
                    end
                end
            end

            ST_POST: begin
                // 触发后只统计有效IQ数据，valid空拍不会改变采样点数量。
                if (data_in_valid) begin
                    write_ptr <= write_ptr_next;

                    if (post_count == (post_count_cfg - 1'b1)) begin
                        post_count     <= post_count_cfg;
                        read_ptr       <= capture_start_ptr;
                        send_remaining <= total_count_cfg;
                        state          <= ST_SEND;
                    end
                    else begin
                        post_count <= post_count + 1'b1;
                    end
                end
            end

            ST_SEND: begin
                // AXI已接收当前输出数据时，释放输出寄存器。
                if (m_axis_tvalid && m_axis_tready) begin
                    m_axis_tvalid <= 1'b0;
                    m_axis_tlast  <= 1'b0;

                    if (m_axis_tlast) begin
                        capture_done        <= 1'b1;
                        captured_sample_num <= total_count_cfg;
                        state               <= ST_DONE;
                    end
                end

                // 发起RAM读请求，同时记录该数据是否为最后一个点。
                if (ram_read_enable) begin
                    ram_read_pending      <= 1'b1;
                    ram_read_last_pending <= (send_remaining == 32'd1);
                    read_ptr              <= read_ptr + 1'b1;
                    send_remaining        <= send_remaining - 1'b1;
                end

                // 一拍后RAM数据有效，再写入AXI输出寄存器。
                if (ram_read_pending) begin
                    m_axis_tdata          <= ram_read_data;
                    m_axis_tvalid         <= 1'b1;
                    m_axis_tlast          <= ram_read_last_pending;
                    ram_read_pending      <= 1'b0;
                    ram_read_last_pending <= 1'b0;
                end
            end

            ST_DONE: begin
                // 完成状态保持，直到软件给出capture_clear。
                m_axis_tvalid <= 1'b0;
                m_axis_tlast  <= 1'b0;
                ram_read_pending <= 1'b0;
            end

            default: begin
                state <= ST_IDLE;
            end
        endcase
    end
end

endmodule
