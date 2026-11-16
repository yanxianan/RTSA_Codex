`timescale 1ns / 1ps

module axi_bram_ctrl(
    input logic_clk,
    input [12:0] axi_bram_addr,
    input axi_bram_clk,
    input [31:0] axi_bram_wd_din,
    output reg [31:0] axi_bram_rd_dout,
    input axi_bram_rst,
    input axi_bram_wr_en,
    input axi_bram_rd_en,
    input cic_cfg_busy,
    // 触发状态由logic_clk时钟域返回，供PS读取。
    input trigger_latched,
    input [9:0] trigger_bin,
    input capture_ready,
    input capture_busy,
    input capture_triggered,
    input capture_done,
    input config_busy,
    input [15:0] active_config_epoch,
    output reg [15:0] dds_phase_step_h,
    output reg [15:0] dds_phase_step_l,
    output reg [3:0] dma_mode,
    output reg [15:0] cic_rate_cfg,
    output reg [7:0] cic_trunc_cfg,
    output reg [2:0] hb_level,
    output reg [15:0] max_hold_frames_cfg,
    // 精简后的触发配置输出。
    output reg trigger_enable,
    output reg capture_arm,
    output reg trigger_clear,
    output reg dma_output_select,
    output reg [9:0] trigger_start_bin,
    output reg [9:0] trigger_end_bin,
    output reg [63:0] trigger_power_threshold,
    output reg [31:0] pre_sample_num,
    output reg [31:0] post_sample_num,

    // Spectrum frame formatter configuration (logic_clk domain).
    output reg [63:0] cfg_center_freq_hz,
    output reg [63:0] cfg_span_hz,
    output reg [7:0]  cfg_trace_mode,
    output reg [15:0] cfg_accum_frames,
    output reg [7:0]  cfg_window_type,
    output reg [31:0] cfg_cal_offset_q16_16,
    output reg [15:0] cfg_config_epoch,
    output wire       config_commit_pulse
);

// AXI BRAM时钟域寄存器。
reg [15:0] r_phase_step_h, r_phase_step_l;
reg [3:0] r_dma_mode;
reg [15:0] r_cic_rate_cfg;
reg [7:0] r_cic_trunc_cfg;
reg [2:0] r_hb_level;
reg [15:0] r_max_hold_frames_cfg;

// 六个触发寄存器，地址紧接当前工程最后一个寄存器0x224。
reg r_trigger_enable;
reg r_dma_output_select;
reg r_capture_arm_toggle;
reg r_trigger_clear_toggle;
reg [9:0] r_trigger_start_bin;
reg [9:0] r_trigger_end_bin;
reg [31:0] r_trigger_threshold_low;
reg [31:0] r_trigger_threshold_high;
reg [12:0] r_pre_sample_num;
reg [12:0] r_post_sample_num;

// 频谱帧头配置寄存器（AXI BRAM 时钟域）。
reg [31:0] r_cfg_center_low;
reg [31:0] r_cfg_center_high;
reg [31:0] r_cfg_span_low;
reg [31:0] r_cfg_span_high;
reg [7:0]  r_cfg_trace_mode;
reg [15:0] r_cfg_accum_frames;
reg [7:0]  r_cfg_window_type;
reg [31:0] r_cfg_cal_offset_q16_16;
reg [15:0] r_cfg_config_epoch;
reg        r_cfg_commit_toggle;

// Commit-time snapshot. PS writes staging registers; a commit copies every
// datapath-relevant staging value into this bank atomically. Only this bank is
// allowed to cross into logic_clk and reach pl_config_latch.
reg [15:0] r_com_phase_step_h, r_com_phase_step_l;
reg [3:0]  r_com_dma_mode;
reg [15:0] r_com_cic_rate_cfg;
reg [7:0]  r_com_cic_trunc_cfg;
reg [2:0]  r_com_hb_level;
reg [15:0] r_com_max_hold_frames_cfg;
reg [31:0] r_com_cfg_center_low, r_com_cfg_center_high;
reg [31:0] r_com_cfg_span_low, r_com_cfg_span_high;
reg [7:0]  r_com_cfg_trace_mode;
reg [15:0] r_com_cfg_accum_frames;
reg [7:0]  r_com_cfg_window_type;
reg [31:0] r_com_cfg_cal_offset_q16_16;
reg [15:0] r_com_cfg_config_epoch;

// Committed配置同步到logic_clk时钟域。
(* ASYNC_REG = "TRUE" *) reg [15:0] r1_com_phase_step_h, r2_com_phase_step_h;
(* ASYNC_REG = "TRUE" *) reg [15:0] r1_com_phase_step_l, r2_com_phase_step_l;
(* ASYNC_REG = "TRUE" *) reg [3:0]  r1_com_dma_mode, r2_com_dma_mode;
(* ASYNC_REG = "TRUE" *) reg [15:0] r1_com_cic_rate_cfg, r2_com_cic_rate_cfg;
(* ASYNC_REG = "TRUE" *) reg [7:0]  r1_com_cic_trunc_cfg, r2_com_cic_trunc_cfg;
(* ASYNC_REG = "TRUE" *) reg [2:0]  r1_com_hb_level, r2_com_hb_level;
(* ASYNC_REG = "TRUE" *) reg [15:0] r1_com_max_hold_frames_cfg, r2_com_max_hold_frames_cfg;

// 触发配置同步到logic_clk时钟域。
reg r1_trigger_enable, r2_trigger_enable;
reg r1_dma_output_select, r2_dma_output_select;
reg [9:0] r1_trigger_start_bin, r2_trigger_start_bin;
reg [9:0] r1_trigger_end_bin, r2_trigger_end_bin;
reg [31:0] r1_trigger_threshold_low, r2_trigger_threshold_low;
reg [31:0] r1_trigger_threshold_high, r2_trigger_threshold_high;
reg [12:0] r1_pre_sample_num, r2_pre_sample_num;
reg [12:0] r1_post_sample_num, r2_post_sample_num;

(* ASYNC_REG = "TRUE" *) reg [31:0] r1_cfg_center_low, r2_cfg_center_low;
(* ASYNC_REG = "TRUE" *) reg [31:0] r1_cfg_center_high, r2_cfg_center_high;
(* ASYNC_REG = "TRUE" *) reg [31:0] r1_cfg_span_low, r2_cfg_span_low;
(* ASYNC_REG = "TRUE" *) reg [31:0] r1_cfg_span_high, r2_cfg_span_high;
(* ASYNC_REG = "TRUE" *) reg [7:0]  r1_cfg_trace_mode, r2_cfg_trace_mode;
(* ASYNC_REG = "TRUE" *) reg [15:0] r1_cfg_accum_frames, r2_cfg_accum_frames;
(* ASYNC_REG = "TRUE" *) reg [7:0]  r1_cfg_window_type, r2_cfg_window_type;
(* ASYNC_REG = "TRUE" *) reg [31:0] r1_cfg_cal_offset_q16_16, r2_cfg_cal_offset_q16_16;
(* ASYNC_REG = "TRUE" *) reg [15:0] r1_cfg_config_epoch, r2_cfg_config_epoch;
(* ASYNC_REG = "TRUE" *) reg r1_cfg_commit_toggle, r2_cfg_commit_toggle;
(* ASYNC_REG = "TRUE" *) reg r3_cfg_commit_toggle, r4_cfg_commit_toggle;
// Data uses a two-flop synchronizer; the toggle uses three flops plus an edge
// detector. The extra toggle stage guarantees that committed data is stable in
// logic_clk at least one cycle before config_commit_pulse is observed.
assign config_commit_pulse = r3_cfg_commit_toggle ^ r4_cfg_commit_toggle;

// 写1命令采用翻转同步，每次写入只产生一个logic_clk周期的脉冲。
reg r1_capture_arm_toggle, r2_capture_arm_toggle, r3_capture_arm_toggle;
reg r1_trigger_clear_toggle, r2_trigger_clear_toggle, r3_trigger_clear_toggle;

// CIC忙状态同步到AXI BRAM时钟域。
reg r_cic_cfg_busy, r1_cic_cfg_busy;
(* ASYNC_REG = "TRUE" *) reg r1_config_busy, r2_config_busy;
(* ASYNC_REG = "TRUE" *) reg [15:0] r1_active_config_epoch, r2_active_config_epoch;

// 触发状态同步到AXI BRAM时钟域。
reg r_trigger_latched, r1_trigger_latched;
reg [9:0] r_trigger_bin, r1_trigger_bin;
reg r_capture_ready, r1_capture_ready;
reg r_capture_busy, r1_capture_busy;
reg r_capture_triggered, r1_capture_triggered;
reg r_capture_done, r1_capture_done;

// axi_bram_rst为高有效，为logic_clk域生成同步释放复位。
(* ASYNC_REG = "TRUE" *) reg [1:0] logic_rst_sync;
wire logic_rst = logic_rst_sync[1];

always @(posedge logic_clk or posedge axi_bram_rst) begin
    if (axi_bram_rst)
        logic_rst_sync <= 2'b11;
    else
        logic_rst_sync <= {logic_rst_sync[0], 1'b0};
end

always @(posedge axi_bram_clk) begin
    if (axi_bram_rst) begin
        // PS暂未写寄存器时的上电默认配置：1.5 MHz DDS、CIC 8倍、HB 4倍、最大保持80帧。
        r_phase_step_h        <= 16'h004E;
        r_phase_step_l        <= 16'hA4A9;
        r_cic_rate_cfg        <= 16'd8;
        r_dma_mode            <= 4'd1;
        r_cic_trunc_cfg       <= 8'd31;
        r_hb_level            <= 3'd2;
        r_max_hold_frames_cfg <= 16'd80;
        r_trigger_enable         <= 1'b0;
        r_dma_output_select      <= 1'b0;
        r_capture_arm_toggle     <= 1'b0;
        r_trigger_clear_toggle   <= 1'b0;
        r_trigger_start_bin      <= 10'd0;
        r_trigger_end_bin        <= 10'd1023;
        r_trigger_threshold_low  <= 32'd0;
        r_trigger_threshold_high <= 32'd0;
        r_pre_sample_num         <= 13'd2048;
        r_post_sample_num        <= 13'd1024;
        r_cfg_center_low         <= 32'd0;
        r_cfg_center_high        <= 32'd0;
        r_cfg_span_low           <= 32'd9765625;
        r_cfg_span_high          <= 32'd0;
        r_cfg_trace_mode         <= 8'd1;
        r_cfg_accum_frames       <= 16'd80;
        r_cfg_window_type        <= 8'd4;
        r_cfg_cal_offset_q16_16  <= -32'sd12451840;
        r_cfg_config_epoch       <= 16'd1;
        r_cfg_commit_toggle      <= 1'b0;
        r_com_phase_step_h       <= 16'h004E;
        r_com_phase_step_l       <= 16'hA4A9;
        r_com_cic_rate_cfg       <= 16'd8;
        r_com_dma_mode           <= 4'd1;
        r_com_cic_trunc_cfg      <= 8'd31;
        r_com_hb_level           <= 3'd2;
        r_com_max_hold_frames_cfg <= 16'd80;
        r_com_cfg_center_low     <= 32'd0;
        r_com_cfg_center_high    <= 32'd0;
        r_com_cfg_span_low       <= 32'd9765625;
        r_com_cfg_span_high      <= 32'd0;
        r_com_cfg_trace_mode     <= 8'd1;
        r_com_cfg_accum_frames   <= 16'd80;
        r_com_cfg_window_type    <= 8'd4;
        r_com_cfg_cal_offset_q16_16 <= -32'sd12451840;
        r_com_cfg_config_epoch   <= 16'd1;
    end
    else if (axi_bram_wr_en && !axi_bram_addr[12]) begin
        case (axi_bram_addr[11:2])
            'h80: r_phase_step_h        <= axi_bram_wd_din[15:0];
            'h81: r_phase_step_l        <= axi_bram_wd_din[15:0];
            // 0x208: reserved. DDS resync is generated only by pl_config_latch.
            'h82: ;
            'h83: r_cic_rate_cfg        <= axi_bram_wd_din[15:0];
            'h84: r_dma_mode            <= axi_bram_wd_din[3:0];
            'h85: r_cic_trunc_cfg       <= axi_bram_wd_din[7:0];
            'h86: r_hb_level            <= axi_bram_wd_din[2:0];
            // 0x21C: reserved. CIC_VALID is generated only by pl_config_latch.
            'h87: ;
            // 0x224：逐频点最大保持累计帧数，写0时数据通路按1帧处理。
            'h89: r_max_hold_frames_cfg <= axi_bram_wd_din[15:0];
            // 0x228：bit0触发使能，bit1武装，bit2清除，bit3选择触发数据。
            'h8a: begin
                r_trigger_enable    <= axi_bram_wd_din[0];
                r_dma_output_select <= axi_bram_wd_din[3];

                if (axi_bram_wd_din[1])
                    r_capture_arm_toggle <= ~r_capture_arm_toggle;

                if (axi_bram_wd_din[2])
                    r_trigger_clear_toggle <= ~r_trigger_clear_toggle;
            end
            // 0x22C：[9:0]起始频点，[25:16]结束频点。
            'h8b: begin
                r_trigger_start_bin <= axi_bram_wd_din[9:0];
                r_trigger_end_bin   <= axi_bram_wd_din[25:16];
            end
            'h8c: r_trigger_threshold_low  <= axi_bram_wd_din;
            'h8d: r_trigger_threshold_high <= axi_bram_wd_din;
            // 0x238：[12:0]触发前点数，[28:16]触发后点数。
            'h8e: begin
                r_pre_sample_num  <= axi_bram_wd_din[12:0];
                r_post_sample_num <= axi_bram_wd_din[28:16];
            end
            // 0x100..0x120：频谱帧头配置。
            'h40: r_cfg_center_low        <= axi_bram_wd_din;
            'h41: r_cfg_center_high       <= axi_bram_wd_din;
            'h42: r_cfg_span_low          <= axi_bram_wd_din;
            'h43: r_cfg_span_high         <= axi_bram_wd_din;
            'h44: begin
                r_cfg_trace_mode   <= axi_bram_wd_din[7:0];
                r_cfg_accum_frames <= axi_bram_wd_din[23:8];
            end
            'h45: r_cfg_window_type       <= axi_bram_wd_din[7:0];
            'h46: r_cfg_cal_offset_q16_16 <= axi_bram_wd_din;
            'h47: r_cfg_config_epoch      <= axi_bram_wd_din[15:0];
            // 0x120 bit0: atomically snapshot every datapath/frame field.
            // The toggle is the only command that reaches pl_config_latch.
            'h48: if (axi_bram_wd_din[0]) begin
                r_cfg_commit_toggle      <= ~r_cfg_commit_toggle;
                r_com_phase_step_h       <= r_phase_step_h;
                r_com_phase_step_l       <= r_phase_step_l;
                r_com_cic_rate_cfg       <= r_cic_rate_cfg;
                r_com_dma_mode           <= r_dma_mode;
                r_com_cic_trunc_cfg      <= r_cic_trunc_cfg;
                r_com_hb_level           <= r_hb_level;
                r_com_max_hold_frames_cfg <= r_max_hold_frames_cfg;
                r_com_cfg_center_low     <= r_cfg_center_low;
                r_com_cfg_center_high    <= r_cfg_center_high;
                r_com_cfg_span_low       <= r_cfg_span_low;
                r_com_cfg_span_high      <= r_cfg_span_high;
                r_com_cfg_trace_mode     <= r_cfg_trace_mode;
                r_com_cfg_accum_frames   <= r_cfg_accum_frames;
                r_com_cfg_window_type    <= r_cfg_window_type;
                r_com_cfg_cal_offset_q16_16 <= r_cfg_cal_offset_q16_16;
                r_com_cfg_config_epoch   <= r_cfg_config_epoch;
            end
            default: ;
        endcase
    end
end

always @(posedge logic_clk) begin
    if (logic_rst) begin
        r1_com_phase_step_h <= 16'h004E; r2_com_phase_step_h <= 16'h004E;
        r1_com_phase_step_l <= 16'hA4A9; r2_com_phase_step_l <= 16'hA4A9;
        r1_com_dma_mode <= 4'd1; r2_com_dma_mode <= 4'd1;
        r1_com_cic_rate_cfg <= 16'd8; r2_com_cic_rate_cfg <= 16'd8;
        r1_com_cic_trunc_cfg <= 8'd31; r2_com_cic_trunc_cfg <= 8'd31;
        r1_com_hb_level <= 3'd2; r2_com_hb_level <= 3'd2;
        r1_com_max_hold_frames_cfg <= 16'd80;
        r2_com_max_hold_frames_cfg <= 16'd80;
        r1_trigger_enable <= 0; r2_trigger_enable <= 0;
        r1_dma_output_select <= 0; r2_dma_output_select <= 0;
        r1_trigger_start_bin <= 0; r2_trigger_start_bin <= 0;
        r1_trigger_end_bin <= 0; r2_trigger_end_bin <= 0;
        r1_trigger_threshold_low <= 0; r2_trigger_threshold_low <= 0;
        r1_trigger_threshold_high <= 0; r2_trigger_threshold_high <= 0;
        r1_pre_sample_num <= 0; r2_pre_sample_num <= 0;
        r1_post_sample_num <= 0; r2_post_sample_num <= 0;
        r1_cfg_center_low <= 32'd0; r2_cfg_center_low <= 32'd0;
        r1_cfg_center_high <= 32'd0; r2_cfg_center_high <= 32'd0;
        r1_cfg_span_low <= 32'd9765625; r2_cfg_span_low <= 32'd9765625;
        r1_cfg_span_high <= 32'd0; r2_cfg_span_high <= 32'd0;
        r1_cfg_trace_mode <= 8'd1; r2_cfg_trace_mode <= 8'd1;
        r1_cfg_accum_frames <= 16'd80; r2_cfg_accum_frames <= 16'd80;
        r1_cfg_window_type <= 8'd4; r2_cfg_window_type <= 8'd4;
        r1_cfg_cal_offset_q16_16 <= -32'sd12451840;
        r2_cfg_cal_offset_q16_16 <= -32'sd12451840;
        r1_cfg_config_epoch <= 16'd1; r2_cfg_config_epoch <= 16'd1;
        r1_cfg_commit_toggle <= 0;
        r2_cfg_commit_toggle <= 0;
        r3_cfg_commit_toggle <= 0;
        r4_cfg_commit_toggle <= 0;
        r1_capture_arm_toggle <= 0;
        r2_capture_arm_toggle <= 0;
        r3_capture_arm_toggle <= 0;
        r1_trigger_clear_toggle <= 0;
        r2_trigger_clear_toggle <= 0;
        r3_trigger_clear_toggle <= 0;
        dds_phase_step_h <= 16'h004E;
        dds_phase_step_l <= 16'hA4A9;
        dma_mode <= 4'd1;
        cic_rate_cfg <= 16'd8;
        cic_trunc_cfg <= 8'd31;
        hb_level <= 3'd2;
        max_hold_frames_cfg <= 16'd80;
        trigger_enable <= 0;
        capture_arm <= 0;
        trigger_clear <= 0;
        dma_output_select <= 0;
        trigger_start_bin <= 0;
        trigger_end_bin <= 0;
        trigger_power_threshold <= 0;
        pre_sample_num <= 0;
        post_sample_num <= 0;
        cfg_center_freq_hz <= 64'd0;
        cfg_span_hz <= 64'd9765625;
        cfg_trace_mode <= 8'd1;
        cfg_accum_frames <= 16'd80;
        cfg_window_type <= 8'd4;
        cfg_cal_offset_q16_16 <= -32'sd12451840;
        cfg_config_epoch <= 16'd1;
    end
    else begin
        r1_com_phase_step_h <= r_com_phase_step_h;
        r1_com_phase_step_l <= r_com_phase_step_l;
        r1_com_dma_mode <= r_com_dma_mode;
        r1_com_cic_rate_cfg <= r_com_cic_rate_cfg;
        r1_com_cic_trunc_cfg <= r_com_cic_trunc_cfg;
        r1_com_hb_level <= r_com_hb_level;
        r1_com_max_hold_frames_cfg <= r_com_max_hold_frames_cfg;
        r1_trigger_enable <= r_trigger_enable;
        r1_dma_output_select <= r_dma_output_select;
        r1_trigger_start_bin <= r_trigger_start_bin;
        r1_trigger_end_bin <= r_trigger_end_bin;
        r1_trigger_threshold_low <= r_trigger_threshold_low;
        r1_trigger_threshold_high <= r_trigger_threshold_high;
        r1_pre_sample_num <= r_pre_sample_num;
        r1_post_sample_num <= r_post_sample_num;
        r1_cfg_center_low <= r_com_cfg_center_low;
        r1_cfg_center_high <= r_com_cfg_center_high;
        r1_cfg_span_low <= r_com_cfg_span_low;
        r1_cfg_span_high <= r_com_cfg_span_high;
        r1_cfg_trace_mode <= r_com_cfg_trace_mode;
        r1_cfg_accum_frames <= r_com_cfg_accum_frames;
        r1_cfg_window_type <= r_com_cfg_window_type;
        r1_cfg_cal_offset_q16_16 <= r_com_cfg_cal_offset_q16_16;
        r1_cfg_config_epoch <= r_com_cfg_config_epoch;
        r1_cfg_commit_toggle <= r_cfg_commit_toggle;
        r1_capture_arm_toggle <= r_capture_arm_toggle;
        r1_trigger_clear_toggle <= r_trigger_clear_toggle;

        r2_com_phase_step_h <= r1_com_phase_step_h;
        r2_com_phase_step_l <= r1_com_phase_step_l;
        r2_com_dma_mode <= r1_com_dma_mode;
        r2_com_cic_rate_cfg <= r1_com_cic_rate_cfg;
        r2_com_cic_trunc_cfg <= r1_com_cic_trunc_cfg;
        r2_com_hb_level <= r1_com_hb_level;
        r2_com_max_hold_frames_cfg <= r1_com_max_hold_frames_cfg;
        r2_trigger_enable <= r1_trigger_enable;
        r2_dma_output_select <= r1_dma_output_select;
        r2_trigger_start_bin <= r1_trigger_start_bin;
        r2_trigger_end_bin <= r1_trigger_end_bin;
        r2_trigger_threshold_low <= r1_trigger_threshold_low;
        r2_trigger_threshold_high <= r1_trigger_threshold_high;
        r2_pre_sample_num <= r1_pre_sample_num;
        r2_post_sample_num <= r1_post_sample_num;
        r2_cfg_center_low <= r1_cfg_center_low;
        r2_cfg_center_high <= r1_cfg_center_high;
        r2_cfg_span_low <= r1_cfg_span_low;
        r2_cfg_span_high <= r1_cfg_span_high;
        r2_cfg_trace_mode <= r1_cfg_trace_mode;
        r2_cfg_accum_frames <= r1_cfg_accum_frames;
        r2_cfg_window_type <= r1_cfg_window_type;
        r2_cfg_cal_offset_q16_16 <= r1_cfg_cal_offset_q16_16;
        r2_cfg_config_epoch <= r1_cfg_config_epoch;
        r2_cfg_commit_toggle <= r1_cfg_commit_toggle;
        r3_cfg_commit_toggle <= r2_cfg_commit_toggle;
        r4_cfg_commit_toggle <= r3_cfg_commit_toggle;
        r2_capture_arm_toggle <= r1_capture_arm_toggle;
        r3_capture_arm_toggle <= r2_capture_arm_toggle;
        r2_trigger_clear_toggle <= r1_trigger_clear_toggle;
        r3_trigger_clear_toggle <= r2_trigger_clear_toggle;

        dds_phase_step_h <= r2_com_phase_step_h;
        dds_phase_step_l <= r2_com_phase_step_l;
        dma_mode <= r2_com_dma_mode;
        cic_rate_cfg <= r2_com_cic_rate_cfg;
        cic_trunc_cfg <= r2_com_cic_trunc_cfg;
        hb_level <= r2_com_hb_level;
        max_hold_frames_cfg <= r2_com_max_hold_frames_cfg;
        trigger_enable <= r2_trigger_enable;
        capture_arm <= r2_capture_arm_toggle ^ r3_capture_arm_toggle;
        trigger_clear <= r2_trigger_clear_toggle ^ r3_trigger_clear_toggle;
        dma_output_select <= r2_dma_output_select;
        trigger_start_bin <= r2_trigger_start_bin;
        trigger_end_bin <= r2_trigger_end_bin;
        trigger_power_threshold <= {
            r2_trigger_threshold_high,
            r2_trigger_threshold_low
        };
        pre_sample_num <= {19'd0, r2_pre_sample_num};
        post_sample_num <= {19'd0, r2_post_sample_num};

        // Committed shadow values are always visible to pl_config_latch.
        // They change in logic_clk one cycle before config_commit_pulse.
        cfg_center_freq_hz <= {r2_cfg_center_high, r2_cfg_center_low};
        cfg_span_hz <= {r2_cfg_span_high, r2_cfg_span_low};
        cfg_trace_mode <= r2_cfg_trace_mode;
        cfg_accum_frames <= r2_cfg_accum_frames;
        cfg_window_type <= r2_cfg_window_type;
        cfg_cal_offset_q16_16 <= r2_cfg_cal_offset_q16_16;
        cfg_config_epoch <= r2_cfg_config_epoch;
    end
end

always @(posedge axi_bram_clk) begin
    if (axi_bram_rst) begin
        r_cic_cfg_busy <= 1'b0;
        r1_cic_cfg_busy <= 1'b0;
        r1_config_busy <= 1'b0;
        r2_config_busy <= 1'b0;
        r1_active_config_epoch <= 16'd0;
        r2_active_config_epoch <= 16'd0;
        r_trigger_latched <= 1'b0;
        r1_trigger_latched <= 1'b0;
        r_trigger_bin <= 10'd0;
        r1_trigger_bin <= 10'd0;
        r_capture_ready <= 1'b0;
        r1_capture_ready <= 1'b0;
        r_capture_busy <= 1'b0;
        r1_capture_busy <= 1'b0;
        r_capture_triggered <= 1'b0;
        r1_capture_triggered <= 1'b0;
        r_capture_done <= 1'b0;
        r1_capture_done <= 1'b0;
    end
    else begin
        r_cic_cfg_busy <= cic_cfg_busy;
        r1_cic_cfg_busy <= r_cic_cfg_busy;
        r1_config_busy <= config_busy;
        r2_config_busy <= r1_config_busy;
        r1_active_config_epoch <= active_config_epoch;
        r2_active_config_epoch <= r1_active_config_epoch;
        r_trigger_latched <= trigger_latched;
        r1_trigger_latched <= r_trigger_latched;
        r_trigger_bin <= trigger_bin;
        r1_trigger_bin <= r_trigger_bin;
        r_capture_ready <= capture_ready;
        r1_capture_ready <= r_capture_ready;
        r_capture_busy <= capture_busy;
        r1_capture_busy <= r_capture_busy;
        r_capture_triggered <= capture_triggered;
        r1_capture_triggered <= r_capture_triggered;
        r_capture_done <= capture_done;
        r1_capture_done <= r_capture_done;
    end
end

always @(posedge axi_bram_clk) begin
    if (axi_bram_rst) begin
        axi_bram_rd_dout <= 32'd0;
    end
    else if (axi_bram_rd_en && !axi_bram_addr[12]) begin
        case (axi_bram_addr[11:2])
            'h80: axi_bram_rd_dout <= {16'd0, r_phase_step_h};
            'h81: axi_bram_rd_dout <= {16'd0, r_phase_step_l};
            'h82: axi_bram_rd_dout <= 32'd0; // reserved: PL-owned DDS resync
            'h83: axi_bram_rd_dout <= {16'd0, r_cic_rate_cfg};
            'h84: axi_bram_rd_dout <= {28'd0, r_dma_mode};
            'h85: axi_bram_rd_dout <= {24'd0, r_cic_trunc_cfg};
            'h86: axi_bram_rd_dout <= {29'd0, r_hb_level};
            'h87: axi_bram_rd_dout <= 32'd0; // reserved: PL-owned CIC_VALID
            'h88: axi_bram_rd_dout <= {31'd0, r1_cic_cfg_busy};
            'h89: axi_bram_rd_dout <= {16'd0, r_max_hold_frames_cfg};
            'h8a: axi_bram_rd_dout <= {
                28'd0,
                r_dma_output_select,
                2'b00,
                r_trigger_enable
            };
            'h8b: axi_bram_rd_dout <= {
                6'd0,
                r_trigger_end_bin,
                6'd0,
                r_trigger_start_bin
            };
            'h8c: axi_bram_rd_dout <= r_trigger_threshold_low;
            'h8d: axi_bram_rd_dout <= r_trigger_threshold_high;
            'h8e: axi_bram_rd_dout <= {
                3'd0,
                r_post_sample_num,
                3'd0,
                r_pre_sample_num
            };
            'h8f: axi_bram_rd_dout <= {
                14'd0,
                r1_trigger_bin,
                3'd0,
                r1_capture_done,
                r1_capture_triggered,
                r1_capture_busy,
                r1_capture_ready,
                r1_trigger_latched
            };
            'h40: axi_bram_rd_dout <= r_cfg_center_low;
            'h41: axi_bram_rd_dout <= r_cfg_center_high;
            'h42: axi_bram_rd_dout <= r_cfg_span_low;
            'h43: axi_bram_rd_dout <= r_cfg_span_high;
            'h44: axi_bram_rd_dout <= {
                8'd0, r_cfg_accum_frames, r_cfg_trace_mode
            };
            'h45: axi_bram_rd_dout <= {24'd0, r_cfg_window_type};
            'h46: axi_bram_rd_dout <= r_cfg_cal_offset_q16_16;
            'h47: axi_bram_rd_dout <= {16'd0, r_cfg_config_epoch};
            'h48: axi_bram_rd_dout <= {31'd0, r_cfg_commit_toggle};
            // 0x130: configuration sequencer busy status.
            'h4c: axi_bram_rd_dout <= {31'd0, r2_config_busy};
            // 0x134: epoch currently selected by the PL frame-boundary latch.
            'h4d: axi_bram_rd_dout <= {16'd0, r2_active_config_epoch};
            default: axi_bram_rd_dout <= 32'd0;
        endcase
    end
end

endmodule
