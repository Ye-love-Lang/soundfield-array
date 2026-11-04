`timescale 1ns/1ps
module tb_fir_control;
  reg clk=0,resetn=0;always #5 clk=~clk;
  reg [7:0] awaddr=0,araddr=0;
  reg [31:0] wdata=0;
  reg [3:0] wstrb=4'hf;
  reg awvalid=0,wvalid=0,bready=0,arvalid=0,rready=0;
  wire awready,wready,bvalid,arready,rvalid;
  wire [1:0] bresp,rresp;wire [31:0] rdata;
  wire sck,ws;
  reg [31:0] value,low,high;
  reg [127:0] test_pcm=0;
  reg test_valid=0;
  integer sample,ch,i,j,timeout;
  mic_filter_axi dut(
    .mic_sd(8'd0),.mic_sck(sck),.mic_ws(ws),
    .s_axi_aclk(clk),.s_axi_aresetn(resetn),
    .s_axi_awaddr(awaddr),.s_axi_awprot(3'd0),.s_axi_awvalid(awvalid),.s_axi_awready(awready),
    .s_axi_wdata(wdata),.s_axi_wstrb(wstrb),.s_axi_wvalid(wvalid),.s_axi_wready(wready),
    .s_axi_bresp(bresp),.s_axi_bvalid(bvalid),.s_axi_bready(bready),
    .s_axi_araddr(araddr),.s_axi_arprot(3'd0),.s_axi_arvalid(arvalid),.s_axi_arready(arready),
    .s_axi_rdata(rdata),.s_axi_rresp(rresp),.s_axi_rvalid(rvalid),.s_axi_rready(rready));
  defparam dut.array_doa.FRAME_SAMPLES=64;
  defparam dut.array_doa.PREPROCESS=0;
  task read32(input [7:0] addr,output [31:0] result);
    begin
      @(negedge clk);araddr=addr;arvalid=1;
      @(posedge clk);while(!arready) @(posedge clk);
      @(negedge clk);arvalid=0;
      wait(rvalid);result=rdata;
      @(negedge clk);rready=1;@(negedge clk);rready=0;
    end
  endtask
  task write32(input [7:0] addr,input [31:0] data);
    begin
      // Exercise independently arriving AW and W channels.
      @(negedge clk);awaddr=addr;awvalid=1;
      @(posedge clk);while(!awready)@(posedge clk);
      @(negedge clk);awvalid=0;
      repeat(2)@(negedge clk);wdata=data;wvalid=1;
      @(posedge clk);while(!wready)@(posedge clk);
      @(negedge clk);wvalid=0;
      wait(bvalid);@(negedge clk);bready=1;@(negedge clk);bready=0;
    end
  endtask
  task frame(input [127:0] x,input signed[15:0] expected);
    begin
      @(negedge clk);test_pcm=x;test_valid=1;
      @(negedge clk);test_valid=0;repeat(2082)@(negedge clk);
      read32(8'h04,value);
      if($signed(value[15:0])!==expected)$fatal(1,"FIR/FIFO value %d expected %d",$signed(value[15:0]),expected);
      read32(8'h00,value);if(!value[0])$fatal(1,"duplicate PCM output during mux change");
    end
  endtask
  task load_coeff(input integer delay_tap);integer a;begin
    write32(8'h90,0);
    for(a=0;a<4096;a=a+1)write32(8'h94,(a%512==delay_tap)?32768:0);
  end endtask
  initial begin
    force dut.array_pcm=test_pcm;force dut.mic_valid=test_valid;
    repeat(10)@(negedge clk);resetn=1;
    read32(8'ha0,value);if(value!==32'h46495231)$fatal(1,"FIR signature");
    write32(8'h14,0);write32(8'h78,3);
    for(i=0;i<34;i=i+1)frame({8{16'd1000}},i<32?0:1000);
    wstrb=1;write32(8'h90,20);wstrb=15;
    read32(8'h90,value);if(value!==0)$fatal(1,"partial strobe changed index");
    write32(8'h98,3);repeat(5)@(negedge clk);
    read32(8'ha8,value);if(value!==1)$fatal(1,"partial commit diagnostic");
    load_coeff(0);
    read32(8'h90,value);if(value!==0)$fatal(1,"upload index wrapping");
    read32(8'h9c,value);if(!value[2])$fatal(1,"bank readiness");
    frame(128'd8000,1000);
    write32(8'h98,3);frame(128'd2000,250);
    read32(8'h9c,value);if(!value[0])$fatal(1,"FIR not active");
    fork
      load_coeff(1);
      begin for(j=0;j<20;j=j+1)frame({8{16'd2000}},2000);end
    join
    read32(8'h9c,value);if(!value[2])$fatal(1,"second bank readiness");
    write32(8'h98,3);frame({8{16'd6000}},2000);
    frame({8{16'd7000}},6000);
    write32(8'h98,2);frame({8{16'd8000}},8000);
    read32(8'h9c,value);if(value[0])$fatal(1,"DAS fallback not restored");
    read32(8'ha4,value);if(value!==0)$fatal(1,"FIR overrun");
    read32(8'hac,value);if(value!==0)$fatal(1,"FIR clipping");
    read32(8'ha8,value);if(value!==1)$fatal(1,"unexpected configuration error");
    read32(8'h28,value);if(value!==0)$fatal(1,"filter overrun");
    read32(8'h08,value);if(value!==0)$fatal(1,"FIR writes alias lowpass index");
    $display("FIR_AXI_PASS signatures,upload,strobe,atomic commit,live update,FIFO single output,DAS fallback");$finish;
  end
  initial begin #10000000;$fatal(1,"FIR AXI watchdog");end
endmodule

