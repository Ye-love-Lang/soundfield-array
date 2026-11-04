`timescale 1ns / 1ps

// Eighteen cascaded Butterworth sections (36th order). A single multiplier is
// reused for all 90 products. Coefficients use signed Q2.29; section samples
// and delay states use signed Q16.16 so intermediate rounding stays small.
// Programmable gain is applied after either the low-pass filter or its bypass.
module biquad_lp (
    input  wire               clk,
    input  wire               resetn,
    input  wire signed [15:0] pcm_in,
    input  wire               pcm_in_valid,
    input  wire        [6:0]  coeff_index,
    input  wire signed [31:0] coeff_value,
    input  wire               coeff_write,
    input  wire               coeff_commit,
    input  wire               filter_enable,
    input  wire        [31:0] gain_q16_16,
    output wire signed [31:0] coeff_readback,
    output reg signed [15:0] pcm_out,
    output reg                pcm_out_valid,
    output reg [31:0]         overrun_count,
    output reg [31:0] limited_count,
    output wire [31:0] actual_gain
);
    localparam integer SECTIONS = 18;
    localparam integer COEFFS = 90;
    localparam [3:0] IDLE = 0, MULT = 1, ACC = 2, FINISH = 3,
                     GAIN = 4, GAIN_MULT = 5, MULT_PREP = 6, GAIN_ENV=7, GAIN_DIV=8;

    reg [3:0] state;
    reg [2:0] term;
    reg [31:0] effective_gain, envelope, dividend, quotient;
    reg [15:0] divisor;
    reg [16:0] remainder;
    reg [5:0] divide_step;
    wire [16:0] raw_abs=raw_sample[15] ? -$signed({raw_sample[15],raw_sample}) : $signed({1'b0,raw_sample});
    wire [31:0] peak_q16={raw_abs[15:0],16'd0};
    wire [31:0] released=envelope-(envelope>>13);
    wire [31:0] next_envelope=peak_q16>released?peak_q16:released;
    wire [31:0] ceiling_peak=(next_envelope+32'd65535)>>16;
    wire [16:0] trial_remainder={remainder[15:0],dividend[31]};
    wire subtract_ok=trial_remainder>={1'b0,divisor};
    wire [31:0] next_quotient={quotient[30:0],subtract_ok};
    assign actual_gain=effective_gain;
    reg filter_enable_q;
    reg [4:0] stage;
    reg signed [31:0] shadow [0:COEFFS-1];
    reg signed [31:0] active [0:COEFFS-1];
    reg signed [31:0] x1 [0:SECTIONS-1], x2 [0:SECTIONS-1];
    reg signed [31:0] y1 [0:SECTIONS-1], y2 [0:SECTIONS-1];
    reg signed [31:0] sample;
    reg signed [15:0] raw_sample;
    reg signed [48:0] gain_product_reg;
    reg signed [63:0] product;
    reg signed [71:0] acc;
    reg signed [31:0] mul_coeff, mul_sample;
    reg signed [31:0] mul_coeff_reg, mul_sample_reg;
    integer i;

    wire [6:0] coeff_addr = ({2'b00, stage} << 2) +
                            {2'b00, stage} + {4'b0000, term};
    wire signed [63:0] multiply = mul_coeff_reg * mul_sample_reg;
    wire signed [71:0] extended_product = {{8{product[63]}}, product};
    wire signed [71:0] rounded = acc + 72'sd268435456;
    wire signed [71:0] scaled = rounded >>> 29;
    wire signed [31:0] stage_sample =
        scaled > 72'sd2147483647 ? 32'sd2147483647 :
        scaled < -72'sd2147483648 ? -32'sd2147483648 : scaled[31:0];
    wire signed [47:0] output_rounded =
        {{16{stage_sample[31]}}, stage_sample} + 48'sd32768;
    wire signed [47:0] output_scaled = output_rounded >>> 16;
    wire signed [15:0] raw_saturated =
        output_scaled > 48'sd32767 ? 16'sd32767 :
        output_scaled < -48'sd32768 ? -16'sd32768 : output_scaled[15:0];
    // Unsigned Q16.16 gain: 65536 is 0 dB, 116541 is about +5 dB.
    wire signed [48:0] gain_product =
        raw_sample * $signed({1'b0, effective_gain});
    wire signed [48:0] gain_scaled =
        (gain_product_reg + 49'sd32768) >>> 16;

    assign coeff_readback = coeff_index < COEFFS ? shadow[coeff_index] : 32'sd0;

    always @* begin
        mul_coeff = active[coeff_addr];
        mul_sample = sample;
        case (term)
            3'd1: mul_sample = x1[stage];
            3'd2: mul_sample = x2[stage];
            3'd3: mul_sample = y1[stage];
            3'd4: mul_sample = y2[stage];
            default: ;
        endcase
    end

    always @(posedge clk) begin
        if (!resetn) begin
            for (i = 0; i < COEFFS; i = i + 1) begin
                shadow[i] <= 0;
                active[i] <= 0;
            end
            for (i = 0; i < SECTIONS; i = i + 1) begin
                x1[i] <= 0; x2[i] <= 0;
                y1[i] <= 0; y2[i] <= 0;
            end
            sample <= 0; raw_sample <= 0; gain_product_reg <= 0;
            product <= 0; acc <= 0;
            mul_coeff_reg <= 0; mul_sample_reg <= 0;
            stage <= 0; term <= 0; state <= IDLE;
            filter_enable_q <= 1'b1;
            pcm_out <= 0; pcm_out_valid <= 0; overrun_count <= 0;
            effective_gain<=65536;envelope<=0;dividend<=0;quotient<=0;
            divisor<=1;remainder<=0;divide_step<=0;limited_count<=0;
        end else begin
            pcm_out_valid <= 0;
            if (coeff_write && coeff_index < COEFFS)
                shadow[coeff_index] <= coeff_value;
            if (coeff_commit) begin
                for (i = 0; i < COEFFS; i = i + 1)
                    active[i] <= shadow[i];
                for (i = 0; i < SECTIONS; i = i + 1) begin
                    x1[i] <= 0; x2[i] <= 0;
                    y1[i] <= 0; y2[i] <= 0;
                end
                state <= IDLE;
            end else if (filter_enable != filter_enable_q) begin
                // Discard one in-flight sample and stale IIR history on a mode
                // change. The next sample starts in the selected mode.
                filter_enable_q <= filter_enable;
                for (i = 0; i < SECTIONS; i = i + 1) begin
                    x1[i] <= 0; x2[i] <= 0;
                    y1[i] <= 0; y2[i] <= 0;
                end
                state <= IDLE;
            end else begin
                if (pcm_in_valid && state != IDLE)
                    overrun_count <= overrun_count + 1'b1;
                case (state)
                    IDLE: if (pcm_in_valid) begin
                        if (filter_enable) begin
                            sample <= {pcm_in, 16'b0};
                            stage <= 0; term <= 0;
                            state <= MULT_PREP;
                        end else begin
                            raw_sample <= pcm_in;
                            state <= GAIN_ENV;
                        end
                    end
                    MULT_PREP: begin
                        mul_coeff_reg <= mul_coeff;
                        mul_sample_reg <= mul_sample;
                        state <= MULT;
                    end
                    MULT: begin
                        product <= multiply;
                        state <= ACC;
                    end
                    ACC: begin
                        if (term == 0) acc <= extended_product;
                        else if (term < 3) acc <= acc + extended_product;
                        else acc <= acc - extended_product;
                        if (term == 4) state <= FINISH;
                        else begin term <= term + 1'b1; state <= MULT_PREP; end
                    end
                    FINISH: begin
                        x2[stage] <= x1[stage]; x1[stage] <= sample;
                        y2[stage] <= y1[stage]; y1[stage] <= stage_sample;
                        if (stage == SECTIONS-1) begin
                            raw_sample <= raw_saturated;
                            state <= GAIN_ENV;
                        end else begin
                            sample <= stage_sample;
                            stage <= stage + 1'b1;
                            term <= 0;
                            state <= MULT_PREP;
                        end
                    end
                    // Peak protection before quantization, with ~170ms release.
                    // A 32-cycle restoring divider keeps the 100MHz timing path short.
                    GAIN_ENV: begin
                        envelope<=next_envelope;
                        divisor<=ceiling_peak==0 ? 16'd1 : ceiling_peak[15:0];
                        dividend<=32'd1966080000; // 30000 * 65536 headroom.
                        quotient<=0;remainder<=0;divide_step<=0;
                        state<=GAIN_DIV;
                    end
                    GAIN_DIV: begin
                        remainder<=subtract_ok ? trial_remainder-{1'b0,divisor} : trial_remainder;
                        dividend<={dividend[30:0],1'b0};
                        quotient<=next_quotient;
                        if(divide_step==31) begin
                            effective_gain<=next_quotient<gain_q16_16 ? next_quotient : gain_q16_16;
                            if(next_quotient<gain_q16_16) limited_count<=limited_count+1'b1;
                            state<=GAIN_MULT;
                        end else divide_step<=divide_step+1'b1;
                    end
                    GAIN_MULT: begin
                        gain_product_reg <= gain_product;
                        state <= GAIN;
                    end
                    GAIN: begin
                        if (gain_scaled > 49'sd32767) pcm_out <= 16'sd32767;
                        else if (gain_scaled < -49'sd32768) pcm_out <= -16'sd32768;
                        else pcm_out <= gain_scaled[15:0];
                        pcm_out_valid <= 1;
                        state <= IDLE;
                    end
                    default: state <= IDLE;
                endcase
            end
        end
    end
endmodule
