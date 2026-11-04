`timescale 1ns/1ps
module raw8_tb;
 reg clk=0;always #5 clk=~clk;
 reg resetn=0,valid=0,arm=0;reg [191:0] pcm=0;reg [12:0] index=0;
 wire signed [23:0] data;wire busy,ready;wire [10:0] frames;
 wire [31:0] generation,first,last,overruns,errors;wire [7:0] clips;
 raw8_snapshot dut(clk,resetn,pcm,valid,arm,index,data,busy,ready,frames,generation,first,last,overruns,errors,clips);
 integer n,c;reg signed [23:0] expected;
 initial begin
  repeat(5)@(negedge clk);resetn=1;arm=1;@(negedge clk);arm=0;
  for(n=0;n<1024;n=n+1)begin
   for(c=0;c<8;c=c+1)pcm[c*24+:24]=(c%2==0)?n*256+c:-(n*256+c);
   valid=1;@(negedge clk);valid=0;repeat(2082)@(negedge clk);
  end
  if(!ready||busy||frames!=1024||generation!=1||last-first!=1023||overruns||errors||clips)$fatal(1,"header");
  for(n=0;n<8192;n=n+1)begin
   index=n;repeat(2)@(negedge clk);
   expected=(n%8%2==0)?(n/8)*256+n%8:-((n/8)*256+n%8);
   if(data!==expected)$fatal(1,"word %d got %d expected %d",n,data,expected);
  end
  // Completed buffer remains frozen while live inputs continue.
  pcm={8{24'h7fffff}};valid=1;@(negedge clk);valid=0;index=0;repeat(20)@(negedge clk);if(data!==24'd0)$fatal(1,"freeze");
  arm=1;@(negedge clk);arm=0;valid=1;@(negedge clk);valid=0;
  repeat(20)@(negedge clk);if(clips!==8'hff)$fatal(1,"clip mask");
  arm=1;@(negedge clk);arm=0;repeat(2)@(negedge clk);if(errors!=1)$fatal(1,"busy arm rejection");
  $display("RAW8_TB_PASS:8192 signed words,order,sequence,freeze,clip,busy-arm");$finish;
 end
 initial begin #30000000;$fatal(1,"timeout");end
endmodule
