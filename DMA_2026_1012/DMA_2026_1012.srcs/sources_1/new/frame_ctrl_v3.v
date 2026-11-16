`timescale 1ns / 1ps

// Overlap-frame controller with whole-frame admission control.
//
// frame_can_accept is sampled only when a new frame becomes due.  Once a
// frame is admitted, all frame_width_reg samples are emitted without being
// interrupted.  If a frame cannot be admitted, the complete frame is
// discarded and the next window start is advanced by one hop.
module frame_ctrl_v3
(
    input  wire         w_rst,
    input  wire         w_clk,
    input  wire         r_rst,
    input  wire         r_clk,

    input  wire [15:0]  frame_width_reg,
    input  wire [15:0]  overlap_ratio_reg,

    input  wire         in_valid,
    input  wire [63:0]  data_in,

    // High means that the downstream FFT input FIFO has room reserved for
    // one complete frame.  This signal is synchronous to r_clk.
    input  wire         frame_can_accept,

    output reg          out_valid,
    output reg  [63:0]  data_out,

    // Status signals for simulation/ILA and later register mapping.
    output reg          frame_drop_pulse,
    output reg  [31:0]  frame_drop_count
);

// The existing frame BRAM is 2048 words deep.
localparam integer FRAME_RAM_ADDR_WIDTH = 11;

wire        input_fifo_prog_full;
wire        input_fifo_full;
wire        input_fifo_prog_empty;
wire        input_fifo_almost_empty;
wire        input_fifo_empty;

reg         frame_ram_we;
reg  [10:0] frame_ram_waddr;
wire [63:0] frame_ram_wdata;
reg  [10:0] frame_ram_raddr;
wire [63:0] frame_ram_rdata;

// Asynchronous input FIFO: w_clk -> r_clk.
fifo_generator_0 async_fifo_u (
    .wr_rst        (w_rst),
    .wr_clk        (w_clk),
    .rd_rst        (r_rst),
    .rd_clk        (r_clk),
    .wr_en         (in_valid),
    .din           (data_in),
    .prog_full     (input_fifo_prog_full),
    .full          (input_fifo_full),
    .rd_en         (frame_ram_we),
    .dout          (frame_ram_wdata),
    .prog_empty    (input_fifo_prog_empty),
    .almost_empty  (input_fifo_almost_empty),
    .empty         (input_fifo_empty)
);

// Drain the input FIFO into the circular frame RAM.  This behavior is kept
// compatible with the original controller.
always @(posedge r_clk) begin
    if (r_rst)
        frame_ram_we <= 1'b0;
    else if (frame_ram_we && input_fifo_almost_empty)
        frame_ram_we <= 1'b0;
    else if (!input_fifo_empty)
        frame_ram_we <= 1'b1;
end

always @(posedge r_clk) begin
    if (r_rst)
        frame_ram_waddr <= {FRAME_RAM_ADDR_WIDTH{1'b0}};
    else if (frame_ram_we)
        frame_ram_waddr <= frame_ram_waddr + 1'b1;
end

blk_mem_gen_1 bram_u (
    .clka   (r_clk),
    .wea    (frame_ram_we),
    .addra  (frame_ram_waddr),
    .dina   (frame_ram_wdata),
    .clkb   (r_clk),
    .addrb  (frame_ram_raddr),
    .doutb  (frame_ram_rdata)
);

// -------------------------------------------------------------------------
// Frame scheduling
// -------------------------------------------------------------------------
wire [15:0] frame_hop = frame_width_reg - overlap_ratio_reg;
wire        frame_config_valid = (frame_width_reg != 16'd0) &&
                                 (frame_width_reg <= 16'd2048) &&
                                 (overlap_ratio_reg < frame_width_reg);

reg         first_frame_pending;
reg  [15:0] hop_sample_count;

// next_frame_start_addr always identifies the first sample of the next frame
// that will become due.  It advances even when a frame is dropped, preventing
// old windows from being transmitted after a long DMA stall.
reg  [10:0] next_frame_start_addr;

reg         frame_read_active;
reg  [10:0] frame_read_count;

wire first_frame_due = frame_config_valid && first_frame_pending &&
                       frame_ram_we &&
                       (frame_ram_waddr == (frame_width_reg - 1'b1));

wire next_frame_due = frame_config_valid && !first_frame_pending &&
                      frame_ram_we &&
                      (hop_sample_count == (frame_hop - 1'b1));

wire frame_due_event   = first_frame_due || next_frame_due;
wire frame_start_event = frame_due_event && frame_can_accept &&
                         !frame_read_active;
wire frame_drop_event  = frame_due_event && !frame_start_event;
wire frame_end_event   = frame_read_active &&
                         (frame_read_count == (frame_width_reg - 1'b1));

always @(posedge r_clk) begin
    if (r_rst)
        first_frame_pending <= 1'b1;
    else if (first_frame_due)
        first_frame_pending <= 1'b0;
end

// Count newly written samples between adjacent frame starts.  For the fixed
// 1024-point, 75-percent-overlap setting, frame_hop is 256 samples.
always @(posedge r_clk) begin
    if (r_rst)
        hop_sample_count <= 16'd0;
    else if (first_frame_due || next_frame_due)
        hop_sample_count <= 16'd0;
    else if (frame_ram_we && !first_frame_pending)
        hop_sample_count <= hop_sample_count + 1'b1;
end

always @(posedge r_clk) begin
    if (r_rst)
        next_frame_start_addr <= {FRAME_RAM_ADDR_WIDTH{1'b0}};
    else if (frame_due_event)
        next_frame_start_addr <= next_frame_start_addr + frame_hop[10:0];
end

// A frame that has started is never paused halfway.  The downstream FIFO has
// already reserved enough free entries for all of its samples.
always @(posedge r_clk) begin
    if (r_rst)
        frame_read_active <= 1'b0;
    else if (frame_end_event)
        frame_read_active <= 1'b0;
    else if (frame_start_event)
        frame_read_active <= 1'b1;
end

always @(posedge r_clk) begin
    if (r_rst)
        frame_read_count <= {FRAME_RAM_ADDR_WIDTH{1'b0}};
    else if (frame_start_event || frame_end_event)
        frame_read_count <= {FRAME_RAM_ADDR_WIDTH{1'b0}};
    else if (frame_read_active)
        frame_read_count <= frame_read_count + 1'b1;
end

always @(posedge r_clk) begin
    if (r_rst)
        frame_ram_raddr <= {FRAME_RAM_ADDR_WIDTH{1'b0}};
    else if (frame_start_event)
        frame_ram_raddr <= next_frame_start_addr;
    else if (frame_read_active)
        frame_ram_raddr <= frame_ram_raddr + 1'b1;
end

// Preserve the original BRAM/output-valid pipeline latency.
reg frame_read_active_d1;
reg frame_read_active_d2;

always @(posedge r_clk) begin
    if (r_rst) begin
        frame_read_active_d1 <= 1'b0;
        frame_read_active_d2 <= 1'b0;
        out_valid            <= 1'b0;
        data_out             <= 64'd0;
    end else begin
        frame_read_active_d1 <= frame_read_active;
        frame_read_active_d2 <= frame_read_active_d1;
        out_valid            <= frame_read_active_d2;
        data_out             <= frame_ram_rdata;
    end
end

always @(posedge r_clk) begin
    if (r_rst) begin
        frame_drop_pulse <= 1'b0;
        frame_drop_count <= 32'd0;
    end else begin
        frame_drop_pulse <= frame_drop_event;
        if (frame_drop_event)
            frame_drop_count <= frame_drop_count + 1'b1;
    end
end

endmodule
