`timescale 1ns / 1ps

// One ICS-43434 in the left I2S slot, driven from a 100 MHz PL clock.
// The phase accumulator gives an average 3.072 MHz SCK / 48 kHz WS with
// 5 ns edge quantization. No derived fabric clock domain is introduced.
module ics43434_rx (
    input  wire               clk,
    input  wire               resetn,
    output reg                mic_sck,
    output reg                mic_ws,
    input  wire               mic_sd,
    output reg signed [15:0] pcm,
    output reg                pcm_valid
);
    localparam [31:0] HALF_SCK_STEP = 32'd263882791;
    reg [31:0] phase;
    wire [32:0] phase_next = {1'b0, phase} + {1'b0, HALF_SCK_STEP};
    reg [5:0] bit_index;
    reg [23:0] shift;
    reg frame_seen;

    always @(posedge clk) begin
        if (!resetn) begin
            phase <= 0;
            mic_sck <= 0;
            mic_ws <= 0;
            bit_index <= 0;
            shift <= 0;
            frame_seen <= 0;
            pcm <= 0;
            pcm_valid <= 0;
        end else begin
            phase <= phase_next[31:0];
            pcm_valid <= 0;
            if (phase_next[32]) begin
                mic_sck <= ~mic_sck;
                if (!mic_sck) begin
                    // Sample after SD has been stable since the last falling
                    // edge. I2S inserts one bit delay after each WS change.
                    if (bit_index >= 6'd1 && bit_index <= 6'd24) begin
                        shift <= {shift[22:0], mic_sd};
                        if (bit_index == 6'd24 && frame_seen) begin
                            // Preserve the signed top 16 bits of the 24-bit
                            // microphone sample; the low 8 bits are dropped.
                            pcm <= {shift[22:0], mic_sd} >>> 8;
                            pcm_valid <= 1;
                        end
                    end
                    if (bit_index == 6'd63) begin
                        bit_index <= 0;
                        frame_seen <= 1;
                    end else begin
                        bit_index <= bit_index + 1'b1;
                    end
                end else begin
                    // WS changes one SCK before the next slot's MSB.
                    if (bit_index == 6'd32) mic_ws <= 1;
                    if (bit_index == 6'd0) mic_ws <= 0;
                end
            end
        end
    end
endmodule
