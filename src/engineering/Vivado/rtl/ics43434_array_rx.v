`timescale 1ns / 1ps
// Eight independent left-slot I2S SD inputs. One counter drives all channels.
// Average 3.072 MHz BCLK / 48 kHz frames from 100 MHz; edge resolution is10ns.
module ics43434_array_rx (
    input wire clk, input wire resetn,
    input wire [7:0] mic_sd,
    output reg mic_sck, output reg mic_ws,
    output reg [127:0] pcm, output reg pcm_valid
);
    localparam [31:0] HALF_SCK_STEP=32'd263882791;
    reg [31:0] phase;
    wire [32:0] phase_next={1'b0,phase}+{1'b0,HALF_SCK_STEP};
    reg [5:0] bit_index;
    reg [23:0] shift[0:7];
    reg frame_seen;
    (* ASYNC_REG="TRUE" *) reg [7:0] sd_meta, sd_sync;
    integer i;
    always @(posedge clk) begin
        if (!resetn) begin
            phase<=0; bit_index<=0; frame_seen<=0;
            mic_sck<=0; mic_ws<=0; pcm<=0; pcm_valid<=0;
            sd_meta<=0; sd_sync<=0;
            for(i=0;i<8;i=i+1) shift[i]<=0;
        end else begin
            sd_meta<=mic_sd; sd_sync<=sd_meta;
            phase<=phase_next[31:0]; pcm_valid<=0;
            if(phase_next[32]) begin
                mic_sck<=~mic_sck;
                if(!mic_sck) begin
                    if(bit_index>=1 && bit_index<=24) begin
                        for(i=0;i<8;i=i+1) begin
                            shift[i]<={shift[i][22:0],sd_sync[i]};
                            if(bit_index==24 && frame_seen)
                                pcm[i*16+:16]<=shift[i][22:7];
                        end
                        if(bit_index==24 && frame_seen) pcm_valid<=1;
                    end
                    if(bit_index==63) begin bit_index<=0;frame_seen<=1;end
                    else bit_index<=bit_index+1'b1;
                end else begin
                    if(bit_index==32) mic_ws<=1;
                    if(bit_index==0) mic_ws<=0;
                end
            end
        end
    end
endmodule
