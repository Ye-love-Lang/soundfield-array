`timescale 1ns/1ps
module tb_array_fir;
 reg clk=0;always #5 clk=~clk;
 reg resetn=0,pcm_valid=0,coeff_write=0,commit=0,enable_in=0;
 reg [127:0] pcm=0;reg [11:0] coeff_index=0;reg signed [19:0] coeff_value=0;
 wire signed [15:0] out_pcm;wire out_valid,enabled,active_bank,bank_ready,busy;
 wire [31:0] overruns,config_errors,clipped_samples;
 array_fir dut(.*);
 reg [127:0] stimulus[0:1299];reg [15:0] golden[0:1299];reg [19:0] coefficients[0:4095];
 integer i,j,max_latency=0,outputs=0;
 task reset_core;begin
  @(negedge clk);resetn=0;pcm_valid=0;coeff_write=0;commit=0;enable_in=0;
  repeat(4)@(negedge clk);resetn=1;repeat(2)@(negedge clk);
 end endtask
 task coeff(input integer address,input signed [19:0] value);begin
  @(negedge clk);coeff_index=address;coeff_value=value;coeff_write=1;
  @(negedge clk);coeff_write=0;
 end endtask
 task upload(input integer kind);integer a;reg signed[19:0] value;begin
  for(a=0;a<4096;a=a+1)begin
   value=0;
   if(kind==0&&a%512==0)value=32768;
   if(kind==1&&a%512==1)value=32768;
   if(kind==2)value=coefficients[a];
   if(kind==3&&a==0)value=524287;
   if(kind==3&&a==512)value=-524288;
   coeff(a,value);
  end
  if(!bank_ready)$fatal(1,"bank not complete");
 end endtask
 task apply_bank(input integer en);begin
  @(negedge clk);enable_in=en;commit=1;
  @(negedge clk);commit=0;
 end endtask
 task sample(input [127:0] x,input signed[15:0] expected);integer latency;begin
  @(negedge clk);pcm=x;pcm_valid=1;
  @(negedge clk);pcm_valid=0;latency=0;
  while(!out_valid&&latency<800)begin @(posedge clk);#1;latency=latency+1;end
  if(!out_valid||out_pcm!==expected)$fatal(1,"sample mismatch output%0d expected%0d latency%0d",out_pcm,expected,latency);
  if(latency>max_latency)max_latency=latency;
  outputs=outputs+1;
  repeat(2083-latency)@(negedge clk);
 end endtask
 initial begin
  $readmemh("C:/Users/lanmy/Documents/Codex/2026-10-13/w/fir_candidate/fir_input.hex",stimulus);
  $readmemh("C:/Users/lanmy/Documents/Codex/2026-10-13/w/fir_candidate/fir_expected.hex",golden);
  $readmemh("C:/Users/lanmy/Documents/Codex/2026-10-13/w/fir_candidate/fir_coefficients.hex",coefficients);
  reset_core;
  coeff(0,32768);apply_bank(1);
  if(config_errors!=1||enabled)$fatal(1,"partial commit accepted");
  upload(0);apply_bank(1);
  coeff(0,0); // Must reject mutation of the pending bank.
  if(config_errors!=2)$fatal(1,"pending bank write accepted");
  sample({8{16'd1000}},1000);
  fork
   upload(1);
   begin for(j=0;j<5;j=j+1)sample({8{16'd2000}},2000);end
  join
  fork
   sample({8{16'd3000}},3000);
   begin repeat(100)@(negedge clk);apply_bank(1);end
  join
  sample({8{16'd4000}},3000);
  if(overruns!=0)$fatal(1,"normal-rate overrun");
  apply_bank(0);
  @(negedge clk);pcm_valid=1;@(negedge clk);pcm_valid=0;
  repeat(600)begin @(posedge clk);#1;if(out_valid)$fatal(1,"disabled FIR output");end
  if(enabled||busy)$fatal(1,"disable not applied");
  reset_core;upload(2);apply_bank(1);
  for(i=0;i<1300;i=i+1)sample(stimulus[i],golden[i]);
  if(overruns||config_errors||clipped_samples)$fatal(1,"unexpected diagnostics in golden run");
  reset_core;upload(3);apply_bank(1);
  pcm=0;pcm[15:0]=32767;pcm[31:16]=-32768;
  sample(pcm,32767);
  pcm=0;pcm[15:0]=-32768;pcm[31:16]=32767;
  sample(pcm,-32768);
  if(clipped_samples!=2)$fatal(1,"saturation counter");
  @(negedge clk);pcm=0;pcm_valid=1;
  @(negedge clk);pcm_valid=0;repeat(10)@(negedge clk);
  pcm_valid=1;@(negedge clk);pcm_valid=0;
  wait(out_valid);#1;if(out_pcm!==0||overruns!=1)$fatal(1,"overrun isolation");
  reset_core;
  repeat(600)begin @(posedge clk);#1;if(out_valid)$fatal(1,"stale output after reset");end
  $display("ARRAY_FIR_PASS golden=1300 max_latency_cycles=%0d outputs=%0d",max_latency,outputs);
  $finish;
 end
 initial begin #100000000;$fatal(1,"watchdog");end
endmodule
