`timescale 1ns / 1ps

// Pipelined 10*log10(re^2 + im^2) converter.
//
// Output is signed 16-bit in 0.01 dB units. The value is relative to a
// full-scale power of 1; the frame header carries the dBFS calibration offset.
// This block intentionally uses a small log2 LUT plus linear interpolation:
// 64 segments over [1,2) give far better than 0.01 dB interpolation error.
module log_magnitude (
    input  wire        clk,
    input  wire        rst,

    input  wire signed [31:0] s_real,
    input  wire signed [31:0] s_imag,
    input  wire        s_valid,

    output reg  signed [15:0] m_value,
    output reg         m_valid
);

// ---------------------------------------------------------------------------
// Stage 1: input registers
// ---------------------------------------------------------------------------
reg signed [31:0] real_s1;
reg signed [31:0] imag_s1;
reg               valid_s1;

always @(posedge clk) begin
    if (rst) begin
        real_s1  <= 32'sd0;
        imag_s1  <= 32'sd0;
        valid_s1 <= 1'b0;
    end else begin
        real_s1  <= s_real;
        imag_s1  <= s_imag;
        valid_s1 <= s_valid;
    end
end

// ---------------------------------------------------------------------------
// Stage 2: register the two squares separately
// ---------------------------------------------------------------------------
(* use_dsp = "yes" *) reg signed [63:0] real_square_s2;
(* use_dsp = "yes" *) reg signed [63:0] imag_square_s2;
reg valid_s2;

always @(posedge clk) begin
    if (rst) begin
        real_square_s2 <= 64'sd0;
        imag_square_s2 <= 64'sd0;
        valid_s2       <= 1'b0;
    end else begin
        real_square_s2 <= real_s1 * real_s1;
        imag_square_s2 <= imag_s1 * imag_s1;
        valid_s2       <= valid_s1;
    end
end

// ---------------------------------------------------------------------------
// Stage 3: power = real^2 + imag^2
// ---------------------------------------------------------------------------
reg [64:0] power_s3;
reg        valid_s3;

always @(posedge clk) begin
    if (rst) begin
        power_s3 <= 65'd0;
        valid_s3 <= 1'b0;
    end else begin
        power_s3 <= {1'b0, real_square_s2} + {1'b0, imag_square_s2};
        valid_s3 <= valid_s2;
    end
end

// ---------------------------------------------------------------------------
// Stage 4: leading-one detection
// ---------------------------------------------------------------------------
reg [6:0] msb_s4;
reg [64:0] power_s4;
reg        zero_s4;
reg        valid_s4;

always @* begin
    if (power_s3[64])      msb_s4 = 7'd64;
    else if (power_s3[63]) msb_s4 = 7'd63;
    else if (power_s3[62]) msb_s4 = 7'd62;
    else if (power_s3[61]) msb_s4 = 7'd61;
    else if (power_s3[60]) msb_s4 = 7'd60;
    else if (power_s3[59]) msb_s4 = 7'd59;
    else if (power_s3[58]) msb_s4 = 7'd58;
    else if (power_s3[57]) msb_s4 = 7'd57;
    else if (power_s3[56]) msb_s4 = 7'd56;
    else if (power_s3[55]) msb_s4 = 7'd55;
    else if (power_s3[54]) msb_s4 = 7'd54;
    else if (power_s3[53]) msb_s4 = 7'd53;
    else if (power_s3[52]) msb_s4 = 7'd52;
    else if (power_s3[51]) msb_s4 = 7'd51;
    else if (power_s3[50]) msb_s4 = 7'd50;
    else if (power_s3[49]) msb_s4 = 7'd49;
    else if (power_s3[48]) msb_s4 = 7'd48;
    else if (power_s3[47]) msb_s4 = 7'd47;
    else if (power_s3[46]) msb_s4 = 7'd46;
    else if (power_s3[45]) msb_s4 = 7'd45;
    else if (power_s3[44]) msb_s4 = 7'd44;
    else if (power_s3[43]) msb_s4 = 7'd43;
    else if (power_s3[42]) msb_s4 = 7'd42;
    else if (power_s3[41]) msb_s4 = 7'd41;
    else if (power_s3[40]) msb_s4 = 7'd40;
    else if (power_s3[39]) msb_s4 = 7'd39;
    else if (power_s3[38]) msb_s4 = 7'd38;
    else if (power_s3[37]) msb_s4 = 7'd37;
    else if (power_s3[36]) msb_s4 = 7'd36;
    else if (power_s3[35]) msb_s4 = 7'd35;
    else if (power_s3[34]) msb_s4 = 7'd34;
    else if (power_s3[33]) msb_s4 = 7'd33;
    else if (power_s3[32]) msb_s4 = 7'd32;
    else if (power_s3[31]) msb_s4 = 7'd31;
    else if (power_s3[30]) msb_s4 = 7'd30;
    else if (power_s3[29]) msb_s4 = 7'd29;
    else if (power_s3[28]) msb_s4 = 7'd28;
    else if (power_s3[27]) msb_s4 = 7'd27;
    else if (power_s3[26]) msb_s4 = 7'd26;
    else if (power_s3[25]) msb_s4 = 7'd25;
    else if (power_s3[24]) msb_s4 = 7'd24;
    else if (power_s3[23]) msb_s4 = 7'd23;
    else if (power_s3[22]) msb_s4 = 7'd22;
    else if (power_s3[21]) msb_s4 = 7'd21;
    else if (power_s3[20]) msb_s4 = 7'd20;
    else if (power_s3[19]) msb_s4 = 7'd19;
    else if (power_s3[18]) msb_s4 = 7'd18;
    else if (power_s3[17]) msb_s4 = 7'd17;
    else if (power_s3[16]) msb_s4 = 7'd16;
    else if (power_s3[15]) msb_s4 = 7'd15;
    else if (power_s3[14]) msb_s4 = 7'd14;
    else if (power_s3[13]) msb_s4 = 7'd13;
    else if (power_s3[12]) msb_s4 = 7'd12;
    else if (power_s3[11]) msb_s4 = 7'd11;
    else if (power_s3[10]) msb_s4 = 7'd10;
    else if (power_s3[9])  msb_s4 = 7'd9;
    else if (power_s3[8])  msb_s4 = 7'd8;
    else if (power_s3[7])  msb_s4 = 7'd7;
    else if (power_s3[6])  msb_s4 = 7'd6;
    else if (power_s3[5])  msb_s4 = 7'd5;
    else if (power_s3[4])  msb_s4 = 7'd4;
    else if (power_s3[3])  msb_s4 = 7'd3;
    else if (power_s3[2])  msb_s4 = 7'd2;
    else if (power_s3[1])  msb_s4 = 7'd1;
    else                   msb_s4 = 7'd0;
end

always @(posedge clk) begin
    if (rst) begin
        power_s4 <= 65'd0;
        zero_s4  <= 1'b1;
        valid_s4 <= 1'b0;
    end else begin
        power_s4 <= power_s3;
        zero_s4  <= (power_s3 == 65'd0);
        valid_s4 <= valid_s3;
    end
end

// ---------------------------------------------------------------------------
// Stage 5: normalize to Q1.63
// ---------------------------------------------------------------------------
reg [63:0] normalized_s5;
reg [6:0]  msb_s5;
reg        zero_s5;
reg        valid_s5;

wire [6:0] shift_s4 = (msb_s4 == 7'd64) ? 7'd0 : (7'd63 - msb_s4);
wire [63:0] normalized_comb_s4 = power_s4[63:0] << shift_s4;

always @(posedge clk) begin
    if (rst) begin
        normalized_s5 <= 64'd0;
        msb_s5        <= 7'd0;
        zero_s5       <= 1'b1;
        valid_s5      <= 1'b0;
    end else begin
        normalized_s5 <= normalized_comb_s4;
        msb_s5        <= msb_s4;
        zero_s5       <= zero_s4;
        valid_s5      <= valid_s4;
    end
end

// ---------------------------------------------------------------------------
// Stage 6: register LUT index and fractional bits
// ---------------------------------------------------------------------------
wire [6:0] idx_s5  = {1'b0, normalized_s5[62:57]};
wire [7:0] frac_s5 = normalized_s5[56:49];

reg [6:0] idx_s6;
reg [7:0] frac_s6;
reg [6:0] msb_s6;
reg       zero_s6;
reg       valid_s6;

always @(posedge clk) begin
    if (rst) begin
        idx_s6   <= 7'd0;
        frac_s6  <= 8'd0;
        msb_s6   <= 7'd0;
        zero_s6  <= 1'b1;
        valid_s6 <= 1'b0;
    end else begin
        idx_s6   <= idx_s5;
        frac_s6  <= frac_s5;
        msb_s6   <= msb_s5;
        zero_s6  <= zero_s5;
        valid_s6 <= valid_s5;
    end
end

function automatic signed [31:0] log2_lut;
    input [6:0] index;
    begin
        case (index)
            7'd0:  log2_lut = 32'sd0;
            7'd1:  log2_lut = 32'sd1466;
            7'd2:  log2_lut = 32'sd2909;
            7'd3:  log2_lut = 32'sd4331;
            7'd4:  log2_lut = 32'sd5732;
            7'd5:  log2_lut = 32'sd7112;
            7'd6:  log2_lut = 32'sd8473;
            7'd7:  log2_lut = 32'sd9814;
            7'd8:  log2_lut = 32'sd11136;
            7'd9:  log2_lut = 32'sd12440;
            7'd10: log2_lut = 32'sd13727;
            7'd11: log2_lut = 32'sd14996;
            7'd12: log2_lut = 32'sd16248;
            7'd13: log2_lut = 32'sd17484;
            7'd14: log2_lut = 32'sd18704;
            7'd15: log2_lut = 32'sd19909;
            7'd16: log2_lut = 32'sd21098;
            7'd17: log2_lut = 32'sd22272;
            7'd18: log2_lut = 32'sd23433;
            7'd19: log2_lut = 32'sd24579;
            7'd20: log2_lut = 32'sd25711;
            7'd21: log2_lut = 32'sd26830;
            7'd22: log2_lut = 32'sd27936;
            7'd23: log2_lut = 32'sd29029;
            7'd24: log2_lut = 32'sd30109;
            7'd25: log2_lut = 32'sd31178;
            7'd26: log2_lut = 32'sd32234;
            7'd27: log2_lut = 32'sd33279;
            7'd28: log2_lut = 32'sd34312;
            7'd29: log2_lut = 32'sd35334;
            7'd30: log2_lut = 32'sd36346;
            7'd31: log2_lut = 32'sd37346;
            7'd32: log2_lut = 32'sd38336;
            7'd33: log2_lut = 32'sd39316;
            7'd34: log2_lut = 32'sd40286;
            7'd35: log2_lut = 32'sd41246;
            7'd36: log2_lut = 32'sd42196;
            7'd37: log2_lut = 32'sd43137;
            7'd38: log2_lut = 32'sd44068;
            7'd39: log2_lut = 32'sd44990;
            7'd40: log2_lut = 32'sd45904;
            7'd41: log2_lut = 32'sd46809;
            7'd42: log2_lut = 32'sd47705;
            7'd43: log2_lut = 32'sd48593;
            7'd44: log2_lut = 32'sd49472;
            7'd45: log2_lut = 32'sd50344;
            7'd46: log2_lut = 32'sd51207;
            7'd47: log2_lut = 32'sd52063;
            7'd48: log2_lut = 32'sd52911;
            7'd49: log2_lut = 32'sd53751;
            7'd50: log2_lut = 32'sd54584;
            7'd51: log2_lut = 32'sd55410;
            7'd52: log2_lut = 32'sd56229;
            7'd53: log2_lut = 32'sd57040;
            7'd54: log2_lut = 32'sd57845;
            7'd55: log2_lut = 32'sd58643;
            7'd56: log2_lut = 32'sd59434;
            7'd57: log2_lut = 32'sd60219;
            7'd58: log2_lut = 32'sd60997;
            7'd59: log2_lut = 32'sd61769;
            7'd60: log2_lut = 32'sd62534;
            7'd61: log2_lut = 32'sd63294;
            7'd62: log2_lut = 32'sd64047;
            7'd63: log2_lut = 32'sd64794;
            7'd64: log2_lut = 32'sd65536;
            default: log2_lut = 32'sd0;
        endcase
    end
endfunction

wire signed [31:0] lut0_s6 = log2_lut(idx_s6);
wire signed [31:0] lut1_s6 = log2_lut(idx_s6 + 7'd1);
wire signed [31:0] diff_s6 = lut1_s6 - lut0_s6;

// ---------------------------------------------------------------------------
// Stage 7: register LUT outputs
// ---------------------------------------------------------------------------
reg signed [31:0] lut0_s7;
reg signed [31:0] diff_s7;
reg [7:0] frac_s7;
reg [6:0] msb_s7;
reg       zero_s7;
reg       valid_s7;

always @(posedge clk) begin
    if (rst) begin
        lut0_s7  <= 32'sd0;
        diff_s7  <= 32'sd0;
        frac_s7  <= 8'd0;
        msb_s7   <= 7'd0;
        zero_s7  <= 1'b1;
        valid_s7 <= 1'b0;
    end else begin
        lut0_s7  <= lut0_s6;
        diff_s7  <= diff_s6;
        frac_s7  <= frac_s6;
        msb_s7   <= msb_s6;
        zero_s7  <= zero_s6;
        valid_s7 <= valid_s6;
    end
end

// ---------------------------------------------------------------------------
// Stage 8: interpolation product
// ---------------------------------------------------------------------------
(* use_dsp = "yes" *) reg signed [40:0] interp_product_s8;
reg signed [31:0] lut0_s8;
reg [6:0] msb_s8;
reg       zero_s8;
reg       valid_s8;

always @(posedge clk) begin
    if (rst) begin
        interp_product_s8 <= 41'sd0;
        lut0_s8           <= 32'sd0;
        msb_s8            <= 7'd0;
        zero_s8           <= 1'b1;
        valid_s8          <= 1'b0;
    end else begin
        interp_product_s8 <= diff_s7 * $signed({1'b0, frac_s7});
        lut0_s8           <= lut0_s7;
        msb_s8            <= msb_s7;
        zero_s8           <= zero_s7;
        valid_s8          <= valid_s7;
    end
end

// ---------------------------------------------------------------------------
// Stage 9: log2(value) in Q16.16
// ---------------------------------------------------------------------------
wire signed [31:0] interp_value_s8 = interp_product_s8 >>> 8;
reg signed [31:0] log2_value_s9;
reg               zero_s9;
reg               valid_s9;

always @(posedge clk) begin
    if (rst) begin
        log2_value_s9 <= 32'sd0;
        zero_s9       <= 1'b1;
        valid_s9      <= 1'b0;
    end else begin
        log2_value_s9 <= ($signed({25'd0, msb_s8}) <<< 16)
                         + lut0_s8 + interp_value_s8;
        zero_s9       <= zero_s8;
        valid_s9      <= valid_s8;
    end
end

// ---------------------------------------------------------------------------
// Stage 10: dB Q16.16 product
// ---------------------------------------------------------------------------
(* use_dsp = "yes" *) reg signed [63:0] db_q16_product_s10;
reg zero_s10;
reg valid_s10;

always @(posedge clk) begin
    if (rst) begin
        db_q16_product_s10 <= 64'sd0;
        zero_s10           <= 1'b1;
        valid_s10          <= 1'b0;
    end else begin
        db_q16_product_s10 <= log2_value_s9 * 64'sd197283;
        zero_s10           <= zero_s9;
        valid_s10          <= valid_s9;
    end
end

// ---------------------------------------------------------------------------
// Stage 11: dB Q16.16 value
// ---------------------------------------------------------------------------
reg signed [31:0] db_q16_value_s11;
reg               zero_s11;
reg               valid_s11;

always @(posedge clk) begin
    if (rst) begin
        db_q16_value_s11 <= 32'sd0;
        zero_s11         <= 1'b1;
        valid_s11        <= 1'b0;
    end else begin
        db_q16_value_s11 <= db_q16_product_s10 >>> 16;
        zero_s11         <= zero_s10;
        valid_s11        <= valid_s10;
    end
end

// ---------------------------------------------------------------------------
// Stage 12: convert dB to 0.01 dB units
// ---------------------------------------------------------------------------
(* use_dsp = "yes" *) reg signed [63:0] value_q0_01_product_s12;
reg zero_s12;
reg valid_s12;

always @(posedge clk) begin
    if (rst) begin
        value_q0_01_product_s12 <= 64'sd0;
        zero_s12                 <= 1'b1;
        valid_s12                <= 1'b0;
    end else begin
        value_q0_01_product_s12 <= db_q16_value_s11 * 64'sd100;
        zero_s12                 <= zero_s11;
        valid_s12                <= valid_s11;
    end
end

// ---------------------------------------------------------------------------
// Stage 13: register final value and clamp to int16
// ---------------------------------------------------------------------------
wire signed [31:0] value_q0_01_s12 = value_q0_01_product_s12 >>> 16;
wire signed [31:0] clamped_s12 =
    value_q0_01_s12 > 32'sd32767 ? 32'sd32767 :
    value_q0_01_s12 < -32'sd32768 ? -32'sd32768 : value_q0_01_s12;

always @(posedge clk) begin
    if (rst) begin
        m_value <= 16'sd0;
        m_valid <= 1'b0;
    end else begin
        m_value <= zero_s12 ? 16'sh8000 : clamped_s12[15:0];
        m_valid <= valid_s12;
    end
end

endmodule
