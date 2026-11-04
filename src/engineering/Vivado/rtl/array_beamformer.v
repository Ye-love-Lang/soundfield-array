`timescale 1ns / 1ps
// Eight-channel fractional delay-and-sum. Q8 delays are committed atomically.
module array_beamformer(
 input wire clk, resetn, input wire [127:0] pcm,
 input wire pcm_valid, input wire [2:0] delay_index,
 input wire [12:0] delay_value, input wire delay_write, commit,
 input wire enable_in, output reg signed [15:0] out_pcm,
 output reg out_valid, output reg [31:0] overruns
);
 wire [127:0] taps0,taps1;
 reg [12:0] shadow[0:7], delays[0:7];
 reg [4:0] write_ptr, base_ptr;
 reg [5:0] filled;
 reg [3:0] channel;
 reg busy, enabled, pending;
 reg [2:0] step;
 reg signed [15:0] sx0,sx1;
 reg signed [9:0] sw0,sw1;
 reg signed [25:0] product0,product1,interpolated;
 reg signed [31:0] sum;
 integer i;
 wire [4:0] tap0=base_ptr-delays[channel[2:0]][12:8];
 wire [4:0] tap1=tap0-5'd1;
 wire signed [15:0] x0=taps0[channel[2:0]*16+:16];
 wire signed [15:0] x1=taps1[channel[2:0]*16+:16];
 genvar g;
 generate for(g=0;g<8;g=g+1) begin: banks
  (* ram_style="distributed" *) reg signed [15:0] history[0:31];
  always @(posedge clk) if(resetn && pcm_valid && !busy)
   history[write_ptr]<=pcm[g*16+:16];
  assign taps0[g*16+:16]=history[tap0];
  assign taps1[g*16+:16]=history[tap1];
 end endgenerate
 wire signed [9:0] w1=$signed({2'b0,delays[channel[2:0]][7:0]});
 wire signed [9:0] w0=10'sd256-w1;
 wire signed [31:0] next_sum=sum+interpolated;
 wire signed [31:0] rounded=(sum+32'sd1024) >>> 11;
 always @(posedge clk) begin
  if(!resetn) begin
   write_ptr<=0;base_ptr<=0;filled<=0;channel<=0;busy<=0;
   enabled<=0;pending<=0;step<=0;sum<=0;
   sx0<=0;sx1<=0;sw0<=0;sw1<=0;product0<=0;product1<=0;interpolated<=0;out_pcm<=0;out_valid<=0;overruns<=0;
   for(i=0;i<8;i=i+1) begin shadow[i]<=0;delays[i]<=0;end
  end else begin
   out_valid<=0;
   if(delay_write) shadow[delay_index]<=delay_value>13'd7936 ? 13'd7936 : delay_value;
   // Latch a complete coefficient bank between audio frames.
   if(commit) pending<=1;
   if(pending && !busy) begin
    for(i=0;i<8;i=i+1) delays[i]<=shadow[i];
    enabled<=enable_in;
    if(!commit) pending<=0;
   end
   if(pcm_valid) begin
    if(busy) overruns<=overruns+1;
    else begin
     base_ptr<=write_ptr;write_ptr<=write_ptr+1'b1;
     if(filled<32) filled<=filled+1'b1;
     if(!enabled) begin out_pcm<=pcm[15:0];out_valid<=1;end
     else if(filled==32) begin channel<=0;step<=0;sum<=0;busy<=1;end
     else begin out_pcm<=0;out_valid<=1;end
    end
   end else if(busy) begin
    case(step)
     0: begin sx0<=x0;sx1<=x1;sw0<=w0;sw1<=w1;step<=1;end
     1: begin product0<=sx0*sw0;product1<=sx1*sw1;step<=2;end
     2: begin interpolated<=product0+product1;step<=3;end
     3: begin
      sum<=next_sum;
      if(channel==7) step<=4;
      else begin channel<=channel+1'b1;step<=0;end
     end
     4: begin
      // Positive interpolation weights sum to 256 and channel weights to 8:
      // the rounded convex average is guaranteed to fit signed 16 bits.
      out_pcm<=rounded[15:0];out_valid<=1;busy<=0;
     end
     default: begin busy<=0;step<=0;end
    endcase
   end
  end
 end
endmodule
