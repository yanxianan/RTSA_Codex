`timescale 1ns / 1ps

// First-stage integration test for DMA_TOP.
//
// Scope of this test:
//   internal DDS -> decimation/overlap FFT -> BD AXIS FIFO -> DMA S_AXIS_S2MM
//
// The DMA S_AXIS_S2MM TREADY net is forced high. This intentionally bypasses
// DMA register programming and DDR writes; it verifies only that a complete
// 1024-beat FFT packet enters the BD FIFO and reaches the DMA stream input
// without data, TKEEP or TLAST corruption.
//
// Add this file as a Simulation Source and select
// tb_DMA_TOP_fifo_path_modified as the simulation top. No text files are
// written by this testbench.

module tb_DMA_TOP;

localparam integer FFT_POINTS       = 1024;
localparam integer FRAME_BYTES      = FFT_POINTS * 8;
localparam time    TEST_TIMEOUT     = 1_000_000ns; // 1 ms

// -------------------------------------------------------------------------
// Simulation clocks and reset
// -------------------------------------------------------------------------
reg sim_r_clk    = 1'b0; // 312.5 MHz
reg sim_w_clk    = 1'b0; // 9.765625 MHz
reg sim_ps_clk   = 1'b0; // 100 MHz DMA/FIFO read-side clock
reg sim_sysref   = 1'b0; // 7.8125 MHz
reg sim_adc_clk  = 1'b0; // RFADC reference clock; unused in DDS mode
reg sim_resetn   = 1'b0;

always #1.600  sim_r_clk   = ~sim_r_clk;
always #51.200 sim_w_clk   = ~sim_w_clk;
always #5.000  sim_ps_clk  = ~sim_ps_clk;
always #64.000 sim_sysref  = ~sim_sysref;
always #5.000  sim_adc_clk = ~sim_adc_clk;

wire [0:0] pl_clk_p;
wire [0:0] pl_clk_n;
wire [0:0] mts_sysref_p;
wire [0:0] mts_sysref_n;

assign pl_clk_p     = sim_r_clk;
assign pl_clk_n     = ~sim_r_clk;
assign mts_sysref_p = sim_sysref;
assign mts_sysref_n = ~sim_sysref;

// -------------------------------------------------------------------------
// DDS register values forced at the DMA_TOP internal control nets
// -------------------------------------------------------------------------
reg [3:0]  sim_dma_mode       = 4'd0;
reg        sim_dds_resync     = 1'b0;
reg [15:0] sim_phase_step_h   = 16'h004E;
reg [15:0] sim_phase_step_l   = 16'hA4A9;

// Unused/reserved top-level outputs.
wire        reg_clk;
wire [11:0] reg_addr;
wire        reg_wr_en;
wire        reg_rd_en;
wire [15:0] reg_data;

DMA_TOP dut (
    .sysref_in_0_diff_n          (~sim_sysref),
    .sysref_in_0_diff_p          ( sim_sysref),
    .vin0_01_0_v_n               (1'b0),
    .vin0_01_0_v_p               (1'b0),
    .vin1_01_0_v_n               (1'b0),
    .vin1_01_0_v_p               (1'b0),
    .adc1_clk_0_clk_n            (~sim_adc_clk),
    .adc1_clk_0_clk_p            ( sim_adc_clk),
    .CLK_DIFF_1_PL_CLK_clk_n     (pl_clk_n),
    .CLK_DIFF_1_PL_CLK_clk_p     (pl_clk_p),
    .CLK_DIFF_2_SYSREF_clk_n     (mts_sysref_n),
    .CLK_DIFF_2_SYSREF_clk_p     (mts_sysref_p),
    .reg_clk                     (reg_clk),
    .reg_addr                    (reg_addr),
    .reg_wr_en                   (reg_wr_en),
    .reg_rd_en                   (reg_rd_en),
    .reg_data                    (reg_data)
);

// -------------------------------------------------------------------------
// Convenient aliases for the two sides of the BD AXIS FIFO
// -------------------------------------------------------------------------
// FIFO S_AXIS: overlap_frame FFT output entering the BD.
wire [63:0] fifo_s_tdata  = dut.m_axis_data_tdata;
wire        fifo_s_tvalid = dut.m_axis_data_tvalid;
wire        fifo_s_tready = dut.m_axis_data_tready;
wire        fifo_s_tlast  = dut.m_axis_data_tlast;

// FIFO M_AXIS: the same net is connected directly to DMA S_AXIS_S2MM.
wire [63:0] dma_s_tdata =
    dut.u0_AXI_DMA_wrapper.AXI_DMA_i.axis_data_fifo_0_M_AXIS_TDATA;
wire [7:0]  dma_s_tkeep =
    dut.u0_AXI_DMA_wrapper.AXI_DMA_i.axis_data_fifo_0_M_AXIS_TKEEP;
wire        dma_s_tvalid =
    dut.u0_AXI_DMA_wrapper.AXI_DMA_i.axis_data_fifo_0_M_AXIS_TVALID;
wire        dma_s_tready =
    dut.u0_AXI_DMA_wrapper.AXI_DMA_i.axis_data_fifo_0_M_AXIS_TREADY;
wire        dma_s_tlast =
    dut.u0_AXI_DMA_wrapper.AXI_DMA_i.axis_data_fifo_0_M_AXIS_TLAST;

// Counters and flags are intentionally visible in the waveform window.
integer fifo_input_beat_count = 0;
integer dma_input_beat_count  = 0;
integer error_count           = 0;

reg saw_fifo_input_handshake = 1'b0;
reg saw_fifo_output_valid    = 1'b0;
reg saw_dma_input_handshake  = 1'b0;
reg saw_nonzero_dma_data     = 1'b0;
reg saw_input_tlast          = 1'b0;
reg saw_dma_tlast            = 1'b0;
reg test_completed           = 1'b0;

// Store the first packet accepted by the FIFO so the read side can be checked.
reg [63:0] expected_data [0:FFT_POINTS-1];
reg [7:0]  expected_keep [0:FFT_POINTS-1];
reg        expected_last [0:FFT_POINTS-1];

// -------------------------------------------------------------------------
// Test-only overrides
// -------------------------------------------------------------------------
// For a deterministic PL-only simulation, bypass the PS/clock-wizard startup.
// These forces do not change synthesis or hardware implementation.
initial begin
    force dut.u0_AXI_DMA_wrapper.AXI_DMA_i.clk_wiz_0_clk_out1 =
        sim_r_clk;
    force dut.u0_AXI_DMA_wrapper.AXI_DMA_i.clk_wiz_0_clk_out2 =
        sim_w_clk;
    force dut.u0_AXI_DMA_wrapper.AXI_DMA_i.zynq_ultra_ps_e_0_pl_clk0 =
        sim_ps_clk;

    force dut.u0_AXI_DMA_wrapper.AXI_DMA_i.
        proc_sys_reset_0_peripheral_aresetn = sim_resetn;
    force dut.u0_AXI_DMA_wrapper.AXI_DMA_i.
        proc_sys_reset_1_peripheral_aresetn = sim_resetn;
    force dut.u0_AXI_DMA_wrapper.AXI_DMA_i.
        rst_ps8_0_99M_peripheral_aresetn = sim_resetn;

    // Bypass PS register writes and select the internal DDS source.
    force dut.dma_mode         = sim_dma_mode;
    force dut.dds_phase_step_h = sim_phase_step_h;
    force dut.dds_phase_step_l = sim_phase_step_l;
    force dut.dds_resync       = sim_dds_resync;

    // Emulate an already configured DMA accepting every FIFO output beat.
    force dut.u0_AXI_DMA_wrapper.AXI_DMA_i.
        axis_data_fifo_0_M_AXIS_TREADY = 1'b1;
end

// Reset release and DDS start sequence.
initial begin
    repeat (32) @(posedge sim_r_clk);
    @(negedge sim_r_clk);
    sim_resetn = 1'b1;

    repeat (16) @(posedge sim_r_clk);
    @(negedge sim_r_clk);
    sim_dma_mode = 4'd1; // MODE_DDS

    // Allow both mode-change reset domains to finish before resynchronizing DDS.
    repeat (16) @(posedge sim_w_clk);
    @(negedge sim_r_clk);
    sim_dds_resync = 1'b1;
    @(negedge sim_r_clk);
    sim_dds_resync = 1'b0;
end

// -------------------------------------------------------------------------
// FIFO write-side monitor
// -------------------------------------------------------------------------
// An input beat is accepted only when TVALID and TREADY are both high on an
// r_clk rising edge.
always @(posedge sim_r_clk) begin
    if (!sim_resetn) begin
        fifo_input_beat_count  = 0;
        saw_fifo_input_handshake = 1'b0;
        saw_input_tlast        = 1'b0;
    end else if (fifo_s_tvalid && fifo_s_tready &&
                 (fifo_input_beat_count < FFT_POINTS)) begin
        saw_fifo_input_handshake = 1'b1;

        expected_data[fifo_input_beat_count] = fifo_s_tdata;
        expected_keep[fifo_input_beat_count] = 8'hFF;
        expected_last[fifo_input_beat_count] = fifo_s_tlast;

        if ((^fifo_s_tdata) === 1'bx) begin
            $error("FIFO input contains X/Z at beat %0d",
                   fifo_input_beat_count);
            error_count = error_count + 1;
        end

        if (fifo_s_tlast) begin
            saw_input_tlast = 1'b1;
            if (fifo_input_beat_count != FFT_POINTS - 1) begin
                $error("FIFO input TLAST at beat %0d; expected %0d",
                       fifo_input_beat_count, FFT_POINTS - 1);
                error_count = error_count + 1;
            end
        end else if (fifo_input_beat_count == FFT_POINTS - 1) begin
            $error("FIFO input is missing TLAST on beat %0d",
                   FFT_POINTS - 1);
            error_count = error_count + 1;
        end

        fifo_input_beat_count = fifo_input_beat_count + 1;
    end
end

// -------------------------------------------------------------------------
// FIFO read side / DMA S_AXIS_S2MM monitor
// -------------------------------------------------------------------------
// With TREADY forced high, every TVALID cycle is an accepted DMA input beat.
always @(posedge sim_ps_clk) begin
    if (!sim_resetn) begin
        dma_input_beat_count = 0;
        saw_fifo_output_valid   = 1'b0;
        saw_dma_input_handshake = 1'b0;
        saw_nonzero_dma_data    = 1'b0;
        saw_dma_tlast           = 1'b0;
    end else begin
        if (dma_s_tvalid)
            saw_fifo_output_valid = 1'b1;

        if (dma_s_tvalid && dma_s_tready &&
            (dma_input_beat_count < FFT_POINTS)) begin
            saw_dma_input_handshake = 1'b1;

            if ((^dma_s_tdata) === 1'bx) begin
                $error("DMA stream input contains X/Z at beat %0d",
                       dma_input_beat_count);
                error_count = error_count + 1;
            end

            if (dma_s_tdata !== expected_data[dma_input_beat_count]) begin
                $error("FIFO data mismatch at beat %0d: in=%h out=%h",
                       dma_input_beat_count,
                       expected_data[dma_input_beat_count], dma_s_tdata);
                error_count = error_count + 1;
            end

            if (dma_s_tkeep !== expected_keep[dma_input_beat_count]) begin
                $error("FIFO TKEEP mismatch at beat %0d: expected=%h got=%h",
                       dma_input_beat_count,
                       expected_keep[dma_input_beat_count], dma_s_tkeep);
                error_count = error_count + 1;
            end

            if (dma_s_tlast !== expected_last[dma_input_beat_count]) begin
                $error("FIFO TLAST mismatch at beat %0d: expected=%b got=%b",
                       dma_input_beat_count,
                       expected_last[dma_input_beat_count], dma_s_tlast);
                error_count = error_count + 1;
            end

            if (dma_s_tdata != 64'd0)
                saw_nonzero_dma_data = 1'b1;

            if (dma_s_tlast) begin
                saw_dma_tlast = 1'b1;
                if (dma_input_beat_count != FFT_POINTS - 1) begin
                    $error("DMA input TLAST at beat %0d; expected %0d",
                           dma_input_beat_count, FFT_POINTS - 1);
                    error_count = error_count + 1;
                end
            end else if (dma_input_beat_count == FFT_POINTS - 1) begin
                $error("DMA input is missing TLAST on beat %0d",
                       FFT_POINTS - 1);
                error_count = error_count + 1;
            end

            dma_input_beat_count = dma_input_beat_count + 1;

            if (dma_input_beat_count == FFT_POINTS) begin
                test_completed = 1'b1;

                if (!saw_fifo_input_handshake ||
                    !saw_fifo_output_valid ||
                    !saw_dma_input_handshake ||
                    !saw_input_tlast ||
                    !saw_dma_tlast ||
                    !saw_nonzero_dma_data) begin
                    $error("One or more required FIFO/DMA observations are missing");
                    error_count = error_count + 1;
                end

                $display("============================================================");
                $display("First-stage FIFO/DMA stream test finished");
                $display("FIFO input beats accepted : %0d", fifo_input_beat_count);
                $display("DMA input beats accepted  : %0d", dma_input_beat_count);
                $display("Packet size               : %0d bytes", FRAME_BYTES);
                $display("FIFO input TLAST observed : %0d", saw_input_tlast);
                $display("DMA input TLAST observed  : %0d", saw_dma_tlast);
                $display("Nonzero DMA data observed : %0d", saw_nonzero_dma_data);
                $display("Error count               : %0d", error_count);
                $display("============================================================");

                if (error_count == 0)
                    $display("PASS: FFT packet crossed the BD FIFO and reached DMA S_AXIS_S2MM");
                else
                    $fatal(1, "FAIL: FIFO/DMA stream test found %0d error(s)",
                           error_count);

                $finish;
            end
        end
    end
end

// Global timeout with diagnostics. The first FFT output appeared around
// 100 us in the earlier module-level simulation; 1 ms leaves ample margin.
initial begin
    #(TEST_TIMEOUT);
    if (!test_completed) begin
        $display("============================================================");
        $display("TIMEOUT after %0t", $time);
        $display("sim_resetn               = %0d", sim_resetn);
        $display("dma_mode                  = %0d", sim_dma_mode);
        $display("FIFO S_AXIS TVALID/TREADY = %0d/%0d",
                 fifo_s_tvalid, fifo_s_tready);
        $display("DMA S_AXIS TVALID/TREADY  = %0d/%0d",
                 dma_s_tvalid, dma_s_tready);
        $display("FIFO input beat count     = %0d", fifo_input_beat_count);
        $display("DMA input beat count      = %0d", dma_input_beat_count);
        $display("input/output TLAST seen   = %0d/%0d",
                 saw_input_tlast, saw_dma_tlast);
        $display("============================================================");
        $fatal(1, "FAIL: no complete FFT packet reached DMA S_AXIS_S2MM");
    end
end

endmodule
