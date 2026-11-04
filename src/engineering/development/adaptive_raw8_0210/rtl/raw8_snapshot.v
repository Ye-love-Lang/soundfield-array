`timescale 1ns/1ps
// Frozen sample-major 1024x8 signed24 capture. Eight writes per 48k frame.
// Capturing and reading never back-pressure the I2S/audio path.
module raw8_snapshot(
 input wire clk,resetn,input wire [191:0] pcm,input wire pcm_valid,
 input wire arm,input wire [12:0] read_index,output reg signed [23:0] read_data,
 output reg busy,ready,output reg [10:0] frames,
 output reg [31:0] generation,first_sample,last_sample,overruns,errors,
 output reg [7:0] clips
);
 (* ram_style="block" *) reg signed [23:0] memory[0:8191];
 reg [191:0] latched;
 reg [2:0] channel;
 reg writing;
 reg [31:0] sample_clock;
 wire signed [23:0] value=latched[channel*24+:24];
 always @(posedge clk) read_data<=memory[read_index];
 always @(posedge clk)begin
  if(!resetn)begin
   busy<=0;ready<=0;frames<=0;generation<=0;first_sample<=0;last_sample<=0;
   overruns<=0;errors<=0;clips<=0;writing<=0;channel<=0;latched<=0;sample_clock<=0;
  end else begin
   if(pcm_valid)sample_clock<=sample_clock+1'b1;
   if(arm)begin
    if(busy||writing)errors<=errors+1'b1;
    else begin busy<=1;ready<=0;frames<=0;clips<=0;end
   end
   if(busy&&pcm_valid)begin
    if(writing)overruns<=overruns+1'b1;
    else begin
     latched<=pcm;channel<=0;writing<=1;
     if(frames==0)first_sample<=sample_clock;
     last_sample<=sample_clock;
    end
   end
   if(writing)begin
    memory[{frames[9:0],channel}]<=value;
    if(value>=24'sd8388000||value<=-24'sd8388000)clips[channel]<=1;
    if(channel==7)begin
     writing<=0;frames<=frames+1'b1;
     if(frames==1023)begin busy<=0;ready<=1;generation<=generation+1'b1;end
    end else channel<=channel+1'b1;
   end
  end
 end
endmodule
