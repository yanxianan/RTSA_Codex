`timescale 1ns / 1ps

// Default-configuration smoke test: DDS -> CIC 8 -> HB 2 -> 80-frame
// max-hold -> 1024-bin formatted frame. No PS register writes are performed.
module tb_hold80_smoke;

localparam integer AXIS_FRAME_WORDS = 8 + 1024 / 4;

reg logic_clk = 1'b0;
reg axi_bram_clk = 1'b0;
reg w_clk = 1'b0;

always #1.600 logic_clk = ~logic_clk;
always #5.000 axi_bram_clk = ~axi_bram_clk;
always #51.200 w_clk = ~w_clk;

reg r_rst = 1'b0;
reg w_rst = 1'b0;
reg axi_bram_rst = 1'b1;
reg [12:0] axi_bram_addr = 13'd0;
reg [31:0] axi_bram_wd_din = 32'd0;
reg axi_bram_en = 1'b0;
reg [3:0] axi_bram_we = 4'd0;
wire [31:0] axi_bram_rd_dout;

wire [63:0] m_axis_data_tdata;
wire [7:0] m_axis_data_tkeep;
wire m_axis_data_tvalid;
wire m_axis_data_tlast;
reg m_axis_data_tready = 1'b1;

logic_top dut (
    .logic_clk(logic_clk), .r_rst(r_rst), .w_clk(w_clk), .w_rst(w_rst),
    .axi_bram_addr(axi_bram_addr), .axi_bram_clk(axi_bram_clk),
    .axi_bram_wd_din(axi_bram_wd_din), .axi_bram_rd_dout(axi_bram_rd_dout),
    .axi_bram_en(axi_bram_en), .axi_bram_rst(axi_bram_rst),
    .axi_bram_we(axi_bram_we),
    .m00_axis_tdata(64'd0), .m00_axis_tvalid(1'b0), .m00_axis_tready(),
    .m01_axis_tdata(64'd0), .m01_axis_tvalid(1'b0), .m01_axis_tready(),
    .m10_axis_tdata(64'd0), .m10_axis_tvalid(1'b0), .m10_axis_tready(),
    .m11_axis_tdata(64'd0), .m11_axis_tvalid(1'b0), .m11_axis_tready(),
    .m_axis_data_tready(m_axis_data_tready),
    .m_axis_data_tdata(m_axis_data_tdata),
    .m_axis_data_tkeep(m_axis_data_tkeep),
    .m_axis_data_tvalid(m_axis_data_tvalid),
    .m_axis_data_tlast(m_axis_data_tlast),
    .reg_clk(), .reg_addr(), .reg_wr_en(), .reg_rd_en(), .reg_data()
);

wire datapath_rst_n = dut.u_spectrum_engine.datapath_r_rst_n;
wire dec_valid = dut.u_spectrum_engine.dec_valid;
wire frame_valid = dut.u_spectrum_engine.u_overlap_fft_module.frame_valid;
wire raw_valid = dut.u_spectrum_engine.fft_raw_tvalid;
wire raw_ready = dut.u_spectrum_engine.fft_raw_tready;
wire raw_last = dut.u_spectrum_engine.fft_raw_tlast;
wire builder_idle = dut.frame_idle;
wire builder_complete = dut.frame_complete;

integer dec_count = 0;
integer frame_count = 0;
integer raw_beat = 0;
integer raw_frames = 0;
integer tx_beat = 0;
integer tx_frames = 0;
integer cycles = 0;

always @(posedge logic_clk) begin
    cycles = cycles + 1;
    if (datapath_rst_n) begin
        if (dec_valid)
            dec_count = dec_count + 1;
        if (frame_valid)
            frame_count = frame_count + 1;
        if (raw_valid && raw_ready) begin
            if (raw_last !== (raw_beat == 1023))
                $fatal(1, "raw TLAST mismatch at bin %0d", raw_beat);
            raw_beat = raw_beat + 1;
            if (raw_beat == 1024) begin
                raw_beat = 0;
                raw_frames = raw_frames + 1;
                if ((raw_frames % 10) == 0)
                    $display("t=%0t ns raw_frames=%0d dec=%0d",
                             $time, raw_frames, dec_count);
            end
        end
        if (m_axis_data_tvalid && m_axis_data_tready) begin
            if (tx_beat == 0 && m_axis_data_tdata[31:0] !== 32'h52545341)
                $fatal(1, "bad magic: %h", m_axis_data_tdata[31:0]);
            if (m_axis_data_tkeep !== 8'hFF)
                $fatal(1, "bad tkeep");
            tx_beat = tx_beat + 1;
            if (m_axis_data_tlast) begin
                if (tx_beat != AXIS_FRAME_WORDS)
                    $fatal(1, "bad frame length: %0d", tx_beat);
                tx_frames = tx_frames + 1;
                $display("PASS: formatted frame %0d at %0t ns (raw=%0d, dec=%0d)",
                         tx_frames, $time, raw_frames, dec_count);
                $finish;
            end
        end
    end
end

initial begin
    #20_000_000;
    $fatal(1, "timeout: raw_frames=%0d dec=%0d frame_valid=%0d tx=%0d builder_idle=%b complete=%b",
           raw_frames, dec_count, frame_count, tx_beat, builder_idle, builder_complete);
end

initial begin
    repeat (10) @(negedge axi_bram_clk);
    axi_bram_rst = 1'b0;
    r_rst = 1'b1;
    w_rst = 1'b1;
end

endmodule
