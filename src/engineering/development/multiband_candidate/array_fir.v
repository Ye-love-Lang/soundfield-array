`timescale 1ns/1ps
// Eight parallel MAC lanes, 512 taps per sample, signed20/Q18 coefficients.
// Ordered upload0..4095 fills only the inactive bank. Commit at next PCM frame.
module array_fir(
 input wire clk,resetn,input wire [127:0] pcm,input wire pcm_valid,
 input wire coeff_write,input wire [11:0] coeff_index,
 input wire signed [19:0] coeff_value,input wire commit,enable_in,
 output reg signed [15:0] out_pcm,output reg out_valid,
 output reg enabled,active_bank,output reg bank_ready,busy,
 output reg [31:0] overruns,config_errors,clipped_samples
);
 reg [12:0] loaded;
 reg pending,pending_enable;
 reg [8:0] write_ptr,base_ptr;
 reg [9:0] filled,issued,completed;
 reg read_valid,product_valid;
 reg [2:0] finish_phase;
 wire accepting=pcm_valid&&!busy;
 wire upload_ok=coeff_write&&!pending&&(coeff_index==0||({1'b0,coeff_index}==loaded&&loaded<4096));
 wire issue=busy&&finish_phase==0&&issued<512;
 wire [8:0] read_addr=base_ptr-issued[8:0];
 wire [9:0] coeff_addr={active_bank,issued[8:0]};
 wire [9:0] upload_addr={~active_bank,coeff_index[8:0]};
 wire [383:0] accumulators;
 genvar g;
 generate for(g=0;g<8;g=g+1)begin: lane
  (* ram_style="block" *) reg signed [15:0] history[0:511];
  (* ram_style="block" *) reg signed [19:0] coefficients[0:1023];
  reg signed [15:0] xq;
  reg signed [19:0] cq;
  reg signed [35:0] product;
  reg signed [47:0] accumulator;
  always @(posedge clk)begin
   if(resetn&&accepting)history[write_ptr]<=pcm[g*16+:16];
   if(resetn&&upload_ok&&coeff_index[11:9]==g)coefficients[upload_addr]<=coeff_value;
   if(issue)begin
    if(issued<filled)xq<=history[read_addr];else xq<=0;
    cq<=coefficients[coeff_addr];
   end
   if(!resetn)begin product<=0;accumulator<=0;end
   else begin
    if(read_valid)product<=xq*cq;
    if(accepting)accumulator<=0;
    else if(product_valid)accumulator<=accumulator+{{12{product[35]}},product};
   end
  end
  assign accumulators[g*48+:48]=accumulator;
 end endgenerate
 reg signed [47:0] pair0,pair1,pair2,pair3,quad0,quad1,total;
 wire signed [47:0] rounded=(total+48'sd131072)>>>18;
 always @(posedge clk)begin
  if(!resetn)begin
   loaded<=0;pending<=0;pending_enable<=0;enabled<=0;active_bank<=0;bank_ready<=0;
   write_ptr<=0;base_ptr<=0;filled<=0;issued<=0;completed<=0;
   read_valid<=0;product_valid<=0;finish_phase<=0;
   out_pcm<=0;out_valid<=0;busy<=0;overruns<=0;config_errors<=0;clipped_samples<=0;
   pair0<=0;pair1<=0;pair2<=0;pair3<=0;quad0<=0;quad1<=0;total<=0;
  end else begin
   out_valid<=0;read_valid<=issue;product_valid<=read_valid;
   // No writes are allowed while a complete bank is pending activation.
   if(coeff_write)begin
    if(upload_ok)begin
     if(coeff_index==0)begin loaded<=1;bank_ready<=0;end
     else begin loaded<=loaded+1'b1;if(coeff_index==4095)bank_ready<=1;end
    end else begin
     config_errors<=config_errors+1'b1;
     // Pending bank must remain intact when a writer violates the handshake.
     if(!pending)begin loaded<=0;bank_ready<=0;end
    end
   end
   if(commit)begin
    if(!pending&&(!enable_in||bank_ready)&&!coeff_write)begin pending<=1;pending_enable<=enable_in;end
    else config_errors<=config_errors+1'b1;
   end
   if(pcm_valid&&busy)overruns<=overruns+1'b1;
   if(accepting)begin
    base_ptr<=write_ptr;write_ptr<=write_ptr+1'b1;
    if(filled<512)filled<=filled+1'b1;
    issued<=0;completed<=0;read_valid<=0;product_valid<=0;finish_phase<=0;
    if(pending)begin
     pending<=0;enabled<=pending_enable;
     if(pending_enable)begin active_bank<=~active_bank;bank_ready<=0;loaded<=0;end
    end
    if(pending?pending_enable:enabled)busy<=1;
   end else if(busy)begin
    if(issue)issued<=issued+1'b1;
    if(product_valid)begin
     completed<=completed+1'b1;
     if(completed==511)finish_phase<=1;
    end
    case(finish_phase)
     1:begin
      pair0<=$signed(accumulators[0+:48])+$signed(accumulators[48+:48]);
      pair1<=$signed(accumulators[96+:48])+$signed(accumulators[144+:48]);
      pair2<=$signed(accumulators[192+:48])+$signed(accumulators[240+:48]);
      pair3<=$signed(accumulators[288+:48])+$signed(accumulators[336+:48]);
      finish_phase<=2;
     end
     2:begin quad0<=pair0+pair1;quad1<=pair2+pair3;finish_phase<=3;end
     3:begin total<=quad0+quad1;finish_phase<=4;end
     4:begin
      if(rounded>32767)begin out_pcm<=32767;clipped_samples<=clipped_samples+1'b1;end
      else if(rounded< -32768)begin out_pcm<=-32768;clipped_samples<=clipped_samples+1'b1;end
      else out_pcm<=rounded[15:0];
      out_valid<=1;busy<=0;finish_phase<=0;
     end
    endcase
   end
  end
 end
endmodule
