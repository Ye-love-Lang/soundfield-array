`timescale 1ns / 1ps

// ICS-43434 + 36th-order low-pass + 4096-sample receive FIFO on AXI4-Lite.
// Register offsets: 00 status (level[28:16], seen[2], overflow[1], empty[0]);
// 04 data (read pops one signed 16-bit sample); 08 coefficient index (0..89);
// 0C coefficient value (write shadow/read back at selected index);
// 10 output gain (unsigned Q16.16, reset to 65536 = 0 dB);
// 14 filter enable (bit 0, reset to 1; gain stays active in both modes);
// 1C commit bit 0; 20 sample count; 24 FIFO overflow count;
// 28 filter overrun count; 3C signature 0x4D494335 ("MIC5").
module mic_filter_axi (
    input  wire [7:0] mic_sd,
    output wire mic_sck,
    output wire mic_ws,
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 s_axi_aclk CLK",
       X_INTERFACE_PARAMETER = "XIL_INTERFACENAME s_axi_aclk, ASSOCIATED_BUSIF S_AXI, ASSOCIATED_RESET s_axi_aresetn, FREQ_HZ 100000000" *)
    input wire s_axi_aclk,
    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 s_axi_aresetn RST",
       X_INTERFACE_PARAMETER = "XIL_INTERFACENAME s_axi_aresetn, POLARITY ACTIVE_LOW" *)
    input wire s_axi_aresetn,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI AWADDR",
       X_INTERFACE_PARAMETER = "XIL_INTERFACENAME S_AXI, PROTOCOL AXI4LITE, DATA_WIDTH 32, ADDR_WIDTH 8, FREQ_HZ 100000000" *)
    input wire [7:0] s_axi_awaddr,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI AWPROT" *)
    input wire [2:0] s_axi_awprot,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI AWVALID" *)
    input wire s_axi_awvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI AWREADY" *)
    output wire s_axi_awready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI WDATA" *)
    input wire [31:0] s_axi_wdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI WSTRB" *)
    input wire [3:0] s_axi_wstrb,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI WVALID" *)
    input wire s_axi_wvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI WREADY" *)
    output wire s_axi_wready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI BRESP" *)
    output wire [1:0] s_axi_bresp,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI BVALID" *)
    output reg s_axi_bvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI BREADY" *)
    input wire s_axi_bready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI ARADDR" *)
    input wire [7:0] s_axi_araddr,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI ARPROT" *)
    input wire [2:0] s_axi_arprot,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI ARVALID" *)
    input wire s_axi_arvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI ARREADY" *)
    output wire s_axi_arready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI RDATA" *)
    output reg [31:0] s_axi_rdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI RRESP" *)
    output wire [1:0] s_axi_rresp,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI RVALID" *)
    output reg s_axi_rvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI RREADY" *)
    input wire s_axi_rready
);
    wire signed [15:0] mic_pcm, filtered_pcm;
    wire mic_valid, filtered_valid;
    wire [31:0] filter_overruns;
    wire [31:0] limited_count,actual_gain;
    reg [6:0] coeff_index;
    reg signed [31:0] coeff_value;
    reg coeff_write, coeff_commit;
    wire signed [31:0] coeff_readback;
    reg [31:0] gain_q16_16;
    reg filter_enable;

    wire [127:0] array_pcm;
    wire beam_valid;
    reg [2:0] beam_index;
    reg [12:0] beam_delay;
    reg beam_write, beam_commit, beam_enable;
    wire [31:0] beam_overruns;
    array_beamformer beamformer(
        .clk(s_axi_aclk), .resetn(s_axi_aresetn), .pcm(array_pcm),
        .pcm_valid(mic_valid), .delay_index(beam_index), .delay_value(beam_delay),
        .delay_write(beam_write), .commit(beam_commit), .enable_in(beam_enable),
        .out_pcm(mic_pcm), .out_valid(beam_valid), .overruns(beam_overruns)
    );
    reg [10:0] corr_index;
    reg [2:0] energy_index;
    reg snapshot_request, snapshot_release;
    wire [63:0] corr_value, energy_value;
    wire [31:0] frozen_sequence, snapshot_status, array_overruns;
    wire [31:0] array_sample_count, channel_activity;
    ics43434_array_rx mic_rx (
        .clk(s_axi_aclk), .resetn(s_axi_aresetn),
        .mic_sck(mic_sck), .mic_ws(mic_ws), .mic_sd(mic_sd),
        .pcm(array_pcm), .pcm_valid(mic_valid)
    );
    mic_array_correlator array_doa (
        .clk(s_axi_aclk), .resetn(s_axi_aresetn),
        .pcm(array_pcm), .pcm_valid(mic_valid),
        .snapshot_request(snapshot_request), .snapshot_release(snapshot_release),
        .corr_index(corr_index), .energy_index(energy_index),
        .corr_value(corr_value), .energy_value(energy_value),
        .frozen_sequence(frozen_sequence), .snapshot_status(snapshot_status),
        .overrun_count(array_overruns), .sample_count(array_sample_count),
        .channel_activity(channel_activity)
    );
    biquad_lp lowpass (
        .clk(s_axi_aclk), .resetn(s_axi_aresetn),
        .pcm_in(mic_pcm), .pcm_in_valid(beam_valid),
        .coeff_index(coeff_index), .coeff_value(coeff_value),
        .coeff_write(coeff_write), .coeff_commit(coeff_commit),
        .filter_enable(filter_enable),
        .gain_q16_16(gain_q16_16),
        .coeff_readback(coeff_readback),
        .pcm_out(filtered_pcm), .pcm_out_valid(filtered_valid),
        .overrun_count(filter_overruns), .limited_count(limited_count), .actual_gain(actual_gain)
    );

    (* ram_style = "block" *) reg [15:0] fifo [0:4095];
    reg [11:0] wr_ptr, rd_ptr;
    reg [12:0] fifo_level;
    reg [31:0] sample_count, overflow_count;
    reg sample_seen;
    wire do_push = filtered_valid && fifo_level < 13'd4096;
    wire do_pop = s_axi_arvalid && s_axi_arready &&
                  s_axi_araddr[7:2] == 6'h01 && fifo_level != 0;

    reg aw_held, w_held;
    reg read_pending;
    reg [7:0] read_address;
    reg [7:0] held_awaddr;
    reg [31:0] held_wdata;
    reg [3:0] held_wstrb;
    assign s_axi_awready = !aw_held && !s_axi_bvalid;
    assign s_axi_wready = !w_held && !s_axi_bvalid;
    assign s_axi_arready = !s_axi_rvalid && !read_pending;
    assign s_axi_bresp = 2'b00;
    assign s_axi_rresp = 2'b00;
    wire unused_prot = ^{s_axi_awprot, s_axi_arprot};

    always @(posedge s_axi_aclk) begin
        if (!s_axi_aresetn) begin
            wr_ptr <= 0; rd_ptr <= 0; fifo_level <= 0;
            sample_count <= 0; overflow_count <= 0; sample_seen <= 0;
            coeff_index <= 0; coeff_value <= 0;
            beam_index<=0;beam_delay<=0;beam_write<=0;beam_commit<=0;beam_enable<=0;
            coeff_write <= 0; coeff_commit <= 0;
            gain_q16_16 <= 32'd65536;
            filter_enable <= 1'b1;
            corr_index<=0;energy_index<=0;snapshot_request<=0;snapshot_release<=0;
            aw_held <= 0; w_held <= 0;
            held_awaddr <= 0; held_wdata <= 0; held_wstrb <= 0;
            s_axi_bvalid <= 0; s_axi_rvalid <= 0; s_axi_rdata <= 0;
            read_pending<=0;read_address<=0;
        end else begin
            coeff_write <= 0; coeff_commit <= 0;beam_write<=0;beam_commit<=0;
            snapshot_request<=0;snapshot_release<=0;
            if (filtered_valid) begin
                sample_seen <= 1;
                sample_count <= sample_count + 1'b1;
                if (!do_push) overflow_count <= overflow_count + 1'b1;
            end
            if (do_push) begin
                fifo[wr_ptr] <= filtered_pcm;
                wr_ptr <= wr_ptr + 1'b1;
            end
            if (do_pop) rd_ptr <= rd_ptr + 1'b1;
            case ({do_push, do_pop})
                2'b10: fifo_level <= fifo_level + 1'b1;
                2'b01: fifo_level <= fifo_level - 1'b1;
                default: ;
            endcase

            if (s_axi_awvalid && s_axi_awready) begin
                held_awaddr <= s_axi_awaddr;
                aw_held <= 1;
            end
            if (s_axi_wvalid && s_axi_wready) begin
                held_wdata <= s_axi_wdata;
                held_wstrb <= s_axi_wstrb;
                w_held <= 1;
            end
            if (aw_held && w_held && !s_axi_bvalid) begin
                if (held_wstrb == 4'hf) begin
                    case (held_awaddr[7:2])
                        4'h2: coeff_index <= held_wdata[6:0];
                        4'h3: begin
                            coeff_value <= held_wdata;
                            coeff_write <= 1;
                        end
                        4'h4: gain_q16_16 <= held_wdata;
                        4'h5: filter_enable <= held_wdata[0];
                        4'h7: if (held_wdata[0]) coeff_commit <= 1;
                        6'h1c: beam_index<=held_wdata[2:0];
                        6'h1d: begin beam_delay<=held_wdata[12:0];beam_write<=1;end
                        6'h1e: begin beam_enable<=held_wdata[0];beam_commit<=held_wdata[1];end
                        6'h12: corr_index<=held_wdata[10:0];
                        6'h15: energy_index<=held_wdata[2:0];
                        6'h18: begin
                            snapshot_request<=held_wdata[0];
                            snapshot_release<=!held_wdata[0];
                        end
                        default: ;
                    endcase
                end
                aw_held <= 0;
                w_held <= 0;
                s_axi_bvalid <= 1;
            end
            if (s_axi_bvalid && s_axi_bready) s_axi_bvalid <= 0;

            if (s_axi_rvalid && s_axi_rready) s_axi_rvalid <= 0;
            // Snapshot correlation storage has synchronous BRAM reads.
            // Delay the response after AR acceptance; index writes complete first.
            if (read_pending) begin
                read_pending<=0;s_axi_rvalid<=1;
                if (read_address[7:2]==6'h13) s_axi_rdata<=corr_value[31:0];
                if (read_address[7:2]==6'h14) s_axi_rdata<=corr_value[63:32];
            end
            if (s_axi_arvalid && s_axi_arready) begin
                read_pending<=1;read_address<=s_axi_araddr;
                case (s_axi_araddr[7:2])
                    4'h0: s_axi_rdata <= {3'b0, fifo_level,
                                          13'b0, sample_seen,
                                          (overflow_count != 0), (fifo_level == 0)};
                    4'h1: s_axi_rdata <= fifo_level != 0 ?
                                          {{16{fifo[rd_ptr][15]}}, fifo[rd_ptr]} : 0;
                    4'h2: s_axi_rdata <= {25'b0, coeff_index};
                    4'h3: s_axi_rdata <= coeff_readback;
                    4'h4: s_axi_rdata <= gain_q16_16;
                    4'h5: s_axi_rdata <= {31'b0, filter_enable};
                    4'h8: s_axi_rdata <= sample_count;
                    4'h9: s_axi_rdata <= overflow_count;
                    4'ha: s_axi_rdata <= filter_overruns;
                    4'hf: s_axi_rdata <= 32'h4d494335;
                    6'h1c:s_axi_rdata<={29'd0,beam_index};
                    6'h1d:s_axi_rdata<={19'd0,beam_delay};
                    6'h1e:s_axi_rdata<={31'd0,beam_enable};
                    6'h1f:s_axi_rdata<=32'h42464d31;
                    6'h20:s_axi_rdata<=beam_overruns;
                    6'h21:s_axi_rdata<=limited_count;
                    6'h22:s_axi_rdata<=actual_gain;
                    6'h23:s_axi_rdata<=32'h4c494d31;
                    6'h10:s_axi_rdata<=32'h41525238;
                    6'h11:s_axi_rdata<=frozen_sequence;
                    6'h12:s_axi_rdata<={21'd0,corr_index};
                    6'h13:s_axi_rdata<=corr_value[31:0];
                    6'h14:s_axi_rdata<=corr_value[63:32];
                    6'h15:s_axi_rdata<={29'd0,energy_index};
                    6'h16:s_axi_rdata<=energy_value[31:0];
                    6'h17:s_axi_rdata<=energy_value[63:32];
                    6'h18:s_axi_rdata<=snapshot_status;
                    6'h19:s_axi_rdata<=array_overruns;
                    6'h1a:s_axi_rdata<=array_sample_count;
                    6'h1b:s_axi_rdata<=channel_activity;
                    default: s_axi_rdata <= 0;
                endcase
            end
        end
    end
endmodule
