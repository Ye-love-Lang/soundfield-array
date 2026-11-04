`timescale 1ns / 1ps
module tb_mic_filter_axi;
    reg clk = 0, resetn = 0;
    always #5 clk = ~clk;
    reg [5:0] awaddr = 0, araddr = 0;
    reg [31:0] wdata = 0;
    reg [3:0] wstrb = 4'hf;
    reg awvalid = 0, wvalid = 0, bready = 0, arvalid = 0, rready = 0;
    wire awready, wready, bvalid, arready, rvalid;
    wire [1:0] bresp, rresp;
    wire [31:0] rdata;
    wire sck, ws;
    reg sd = 0;
    reg [31:0] result;
    mic_filter_axi dut (
        .mic_sd(sd), .mic_sck(sck), .mic_ws(ws),
        .s_axi_aclk(clk), .s_axi_aresetn(resetn),
        .s_axi_awaddr(awaddr), .s_axi_awprot(3'b0),
        .s_axi_awvalid(awvalid), .s_axi_awready(awready),
        .s_axi_wdata(wdata), .s_axi_wstrb(wstrb),
        .s_axi_wvalid(wvalid), .s_axi_wready(wready),
        .s_axi_bresp(bresp), .s_axi_bvalid(bvalid), .s_axi_bready(bready),
        .s_axi_araddr(araddr), .s_axi_arprot(3'b0),
        .s_axi_arvalid(arvalid), .s_axi_arready(arready),
        .s_axi_rdata(rdata), .s_axi_rresp(rresp),
        .s_axi_rvalid(rvalid), .s_axi_rready(rready)
    );

    task read32(input [5:0] addr, output [31:0] value);
        begin
            @(negedge clk); araddr = addr; arvalid = 1;
            @(posedge clk); while (!arready) @(posedge clk);
            @(negedge clk); arvalid = 0;
            wait(rvalid); value = rdata;
            @(negedge clk); rready = 1;
            @(negedge clk); rready = 0;
        end
    endtask

    task write32(input [5:0] addr, input [31:0] value);
        begin
            @(negedge clk); awaddr = addr; wdata = value;
            awvalid = 1; wvalid = 1;
            @(posedge clk); while (!awready || !wready) @(posedge clk);
            @(negedge clk); awvalid = 0; wvalid = 0;
            wait(bvalid);
            @(negedge clk); bready = 1;
            @(negedge clk); bready = 0;
        end
    endtask

    initial begin
        repeat (5) @(negedge clk);
        resetn = 1;
        read32(6'h3c, result);
        if (result !== 32'h4d494335) $fatal(1, "signature %h", result);
        read32(6'h10, result);
        if (result !== 32'd65536) $fatal(1, "default gain %d", result);
        write32(6'h10, 32'd116541);
        read32(6'h10, result);
        if (result !== 32'd116541) $fatal(1, "gain readback %d", result);
        read32(6'h14, result);
        if (result !== 32'd1) $fatal(1, "filter should default enabled");
        write32(6'h14, 32'd0);
        read32(6'h14, result);
        if (result !== 32'd0) $fatal(1, "filter disable readback %d", result);
        read32(6'h10, result);
        if (result !== 32'd116541) $fatal(1, "gain changed on bypass %d", result);
        write32(6'h14, 32'd1);
        read32(6'h14, result);
        if (result !== 32'd1) $fatal(1, "filter re-enable readback %d", result);
        read32(6'h00, result);
        if (!result[0]) $fatal(1, "FIFO should be empty");
        write32(6'h08, 32'd54);
        write32(6'h0c, 32'd123456);
        read32(6'h0c, result);
        if (result !== 32'd123456) $fatal(1, "coefficient readback %d", result);
        write32(6'h1c, 32'd1);
        @(negedge clk);
        force dut.filtered_valid = 1'b1;
        force dut.filtered_pcm = 16'sh9234;
        @(negedge clk);
        release dut.filtered_valid;
        release dut.filtered_pcm;
        read32(6'h00, result);
        if (result[0] || result[28:16] !== 13'd1)
            $fatal(1, "FIFO status %h", result);
        read32(6'h04, result);
        if (result !== 32'hffff9234) $fatal(1, "sample %h", result);
        read32(6'h00, result);
        if (!result[0]) $fatal(1, "FIFO did not pop");
        $display("PASS tb_mic_filter_axi");
        $finish;
    end
endmodule
