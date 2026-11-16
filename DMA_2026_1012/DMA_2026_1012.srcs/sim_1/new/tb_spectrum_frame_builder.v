`timescale 1ns / 1ps

module tb_spectrum_frame_builder;

localparam integer BIN_COUNT = 1024;
localparam integer HEADER_WORDS = 8;
localparam integer PAYLOAD_WORDS = BIN_COUNT / 4;
localparam integer TOTAL_WORDS = HEADER_WORDS + PAYLOAD_WORDS;
localparam [63:0] CFG_CENTER_HZ = 64'd1000000000;
localparam [63:0] CFG_SPAN_HZ = 64'd200000000;
localparam [7:0]  CFG_TRACE_MODE = 8'd2;
localparam [15:0] CFG_ACCUM_FRAMES = 16'd16;
localparam [7:0]  CFG_WINDOW_TYPE = 8'd1;
localparam [31:0] CFG_CAL_Q16_16 = 32'sd0;
localparam [15:0] CFG_EPOCH = 16'd7;

reg clk;
reg rst;
reg [63:0] s_tdata;
reg s_tvalid;
wire s_tready;
reg s_tlast;

wire [63:0] m_tdata;
wire [7:0] m_tkeep;
wire m_tvalid;
reg m_tready;
wire m_tlast;
wire frame_idle;
wire frame_complete;

spectrum_frame_builder #(
    .BIN_COUNT(BIN_COUNT)
) dut (
    .clk(clk),
    .rst(rst),
    .cfg_center_freq_hz(CFG_CENTER_HZ),
    .cfg_span_hz(CFG_SPAN_HZ),
    .cfg_trace_mode(CFG_TRACE_MODE),
    .cfg_accum_frames(CFG_ACCUM_FRAMES),
    .cfg_window_type(CFG_WINDOW_TYPE),
    .cfg_cal_offset_q16_16(CFG_CAL_Q16_16),
    .cfg_config_epoch(CFG_EPOCH),
    .cfg_hold(1'b0),
    .s_axis_tdata(s_tdata),
    .s_axis_tvalid(s_tvalid),
    .s_axis_tready(s_tready),
    .s_axis_tlast(s_tlast),
    .m_axis_tdata(m_tdata),
    .m_axis_tkeep(m_tkeep),
    .m_axis_tvalid(m_tvalid),
    .m_axis_tready(m_tready),
    .m_axis_tlast(m_tlast),
    .frame_idle(frame_idle),
    .frame_complete(frame_complete)
);

always #5 clk = ~clk;

integer input_index;
integer output_index;
integer payload_index;
integer errors;
reg input_finished;
reg [63:0] header_words [0:HEADER_WORDS-1];

always @(posedge clk) begin
    if (rst) begin
        input_index <= 0;
        s_tvalid <= 1'b0;
        s_tdata <= 64'd0;
        s_tlast <= 1'b0;
        input_finished <= 1'b0;
    end else begin
        if (!input_finished) begin
            s_tvalid <= 1'b1;
            s_tdata <= {32'sd0, 32'sd1000};
            s_tlast <= (input_index == BIN_COUNT - 1);
        end else begin
            s_tvalid <= 1'b0;
            s_tlast <= 1'b0;
        end
        if (s_tvalid && s_tready) begin
            if (input_index < BIN_COUNT) begin
                input_index <= input_index + 1;
            end
            if (input_index == BIN_COUNT - 1) begin
                input_finished <= 1'b1;
                s_tvalid <= 1'b0;
                s_tlast <= 1'b0;
            end
        end
    end
end

always @(posedge clk) begin
    #1;
    if (!rst && m_tvalid && m_tready && output_index < TOTAL_WORDS) begin
        // $display("CAP t=%0t idx=%0d m=%h f0=%0d f1=%0d", $time, output_index,
        //          m_tdata, dut.fifo_mem[0], dut.fifo_mem[1]);
        if (output_index < HEADER_WORDS) begin
            header_words[output_index] = m_tdata;
        end else begin
            payload_index = output_index - HEADER_WORDS;
            if ($signed(m_tdata[15:0]) < 5900
                || $signed(m_tdata[15:0]) > 6100
                || $signed(m_tdata[31:16]) < 5900
                || $signed(m_tdata[31:16]) > 6100
                || $signed(m_tdata[47:32]) < 5900
                || $signed(m_tdata[47:32]) > 6100
                || $signed(m_tdata[63:48]) < 5900
                || $signed(m_tdata[63:48]) > 6100) begin
                $error("payload[%0d] log values out of range: %0d %0d %0d %0d",
                       payload_index, $signed(m_tdata[15:0]),
                       $signed(m_tdata[31:16]), $signed(m_tdata[47:32]),
                       $signed(m_tdata[63:48]));
                errors = errors + 1;
            end
        end

        if (m_tlast !== (output_index == TOTAL_WORDS - 1)) begin
            $error("TLAST at output %0d, expected last at %0d",
                   output_index, TOTAL_WORDS - 1);
            errors = errors + 1;
        end
        output_index = output_index + 1;
    end
end

always @(posedge clk) begin
    if ($time < 500) begin
        $display("t=%0t state=%0d in=%0d srdy=%b fifo=%0d wr=%0d rd=%0d logv=%b logval=%0d outv=%b outlast=%b m=%h f0=%0d f1=%0d f2=%0d",
                 $time, dut.state, dut.input_count, s_tready, dut.fifo_count,
                 dut.fifo_wr_ptr, dut.fifo_rd_ptr, dut.log_valid, $signed(dut.log_value),
                 m_tvalid, m_tlast, m_tdata, dut.fifo_mem[0], dut.fifo_mem[1],
                 dut.fifo_mem[2]);
    end
end

initial begin
    clk = 1'b0;
    rst = 1'b1;
    s_tdata = 64'd0;
    s_tvalid = 1'b0;
    s_tlast = 1'b0;
    m_tready = 1'b1;
    errors = 0;
    output_index = 0;
    payload_index = 0;

    repeat (5) @(posedge clk);
    rst <= 1'b0;

    wait (output_index == TOTAL_WORDS);
    repeat (5) @(posedge clk);

    if (header_words[0][31:0] !== 32'h52545341) begin
        $error("bad magic 0x%08h", header_words[0][31:0]);
        errors = errors + 1;
    end
    if (header_words[0][47:32] !== 16'd1) begin
        $error("bad version %0d", header_words[0][47:32]);
        errors = errors + 1;
    end
    if (header_words[0][63:48] !== 16'd64) begin
        $error("bad header bytes %0d", header_words[0][63:48]);
        errors = errors + 1;
    end
    if (header_words[1][31:0] !== 32'd2112) begin
        $error("bad frame bytes %0d", header_words[1][31:0]);
        errors = errors + 1;
    end
    if (header_words[1][63:32] !== 32'h00000101) begin
        $error("bad flags 0x%08h", header_words[1][63:32]);
        errors = errors + 1;
    end
    if (header_words[4] !== CFG_CENTER_HZ) begin
        $error("bad center %0d", header_words[4]);
        errors = errors + 1;
    end
    if (header_words[5] !== CFG_SPAN_HZ) begin
        $error("bad span %0d", header_words[5]);
        errors = errors + 1;
    end
    if (header_words[6][31:0] !== 32'd1024) begin
        $error("bad bin count %0d", header_words[6][31:0]);
        errors = errors + 1;
    end
    if (header_words[6][39:32] !== CFG_TRACE_MODE) begin
        $error("bad trace mode %0d", header_words[6][39:32]);
        errors = errors + 1;
    end
    if (header_words[6][55:40] !== CFG_ACCUM_FRAMES) begin
        $error("bad accumulation %0d", header_words[6][55:40]);
        errors = errors + 1;
    end
    if (header_words[6][63:56] !== CFG_WINDOW_TYPE) begin
        $error("bad window type %0d", header_words[6][63:56]);
        errors = errors + 1;
    end
    if (header_words[7][31:0] !== CFG_CAL_Q16_16) begin
        $error("bad calibration %0d", $signed(header_words[7][31:0]));
        errors = errors + 1;
    end
    if (header_words[7][63:48] !== CFG_EPOCH) begin
        $error("bad epoch %0d", header_words[7][63:48]);
        errors = errors + 1;
    end

    if (errors == 0) begin
        $display("PASS: spectrum_frame_builder header/payload/TLAST passed");
    end else begin
        $fatal(1, "FAIL: %0d error(s)", errors);
    end
    $finish;
end

endmodule
