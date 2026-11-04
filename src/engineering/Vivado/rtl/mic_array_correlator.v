`timescale 1ns/1ps
// Eight channels, all 28 lexicographic pairs, 37 lags (-18..18).
// R_ij(l)=sum x_i[t-18]*x_j[t-18-l], over FRAME_SAMPLES samples.
// One pipelined MAC job per clock: 1036 correlations + 8 energies in <1050
// clocks, below the 2083-clock 48kHz interval. Result arrays infer BRAM.
module mic_array_correlator #(
    parameter integer FRAME_SAMPLES=1024,
    parameter integer PREPROCESS=1
)(
    input wire clk, input wire resetn,
    input wire [127:0] pcm, input wire pcm_valid,
    input wire snapshot_request, input wire snapshot_release,
    input wire [10:0] corr_index, input wire [2:0] energy_index,
    output wire [63:0] corr_value, output wire [63:0] energy_value,
    output reg [31:0] frozen_sequence,
    output wire [31:0] snapshot_status,
    output reg [31:0] overrun_count, output reg [31:0] sample_count,
    output wire [31:0] channel_activity
);
    localparam CORRELATIONS=1036, JOBS=1044;
    (* ram_style="distributed" *) reg signed [15:0] history[0:511];
    (* ram_style="block" *) reg signed [47:0] accum[0:2047];
    (* ram_style="block" *) reg signed [47:0] latest[0:2047];
    (* ram_style="block" *) reg signed [47:0] frozen[0:2047];
    reg [47:0] latest_energy[0:7], frozen_energy[0:7];
    reg signed [31:0] dc[0:7],lp[0:7];
    reg [5:0] wr_ptr,center_ptr,warmup,lag_index;
    reg [15:0] frame_sample;
    reg busy,issuing,first_sample,last_sample;
    reg [10:0] issue_job,job1,job2,job3;
    reg [2:0] pair_i,pair_j;
    reg valid1,valid2,valid3;
    reg signed [15:0] operand_a,operand_b;
    reg signed [31:0] product2;
    reg signed [47:0] old1,old2,sum3;
    wire signed [47:0] extended_product={{16{product2[31]}},product2};
    wire [2:0] ci=issue_job<CORRELATIONS ? pair_i : issue_job-CORRELATIONS;
    wire [2:0] cj=issue_job<CORRELATIONS ? pair_j : issue_job-CORRELATIONS;
    wire [5:0] second_ptr=issue_job<CORRELATIONS ? center_ptr+6'd18-lag_index : center_ptr;
    wire [8:0] hist_a={ci,center_ptr},hist_b={cj,second_ptr};
    reg latest_valid,pending,copy_active,frozen_valid;
    reg [10:0] copy_index,copy_destination;
    reg copy_read_valid;
    reg signed [47:0] copy_data,selected_corr;
    reg [31:0] latest_sequence;
    reg [7:0] frame_clip,latest_clip,frozen_clip;
    wire [7:0] input_clip;
    wire [127:0] conditioned;
    genvar g;
    generate for(g=0;g<8;g=g+1) begin: conditioning
        wire signed [15:0] raw=pcm[g*16+:16];
        wire signed [31:0] raw_q8={{8{raw[15]}},raw,8'b0};
        wire signed [31:0] hp=raw_q8-dc[g];
        wire signed [31:0] next_lp=lp[g]+((hp-lp[g])>>>2);
        wire signed [31:0] reduced=next_lp>>>8;
        assign conditioned[g*16+:16]=PREPROCESS==0 ? raw :
            reduced>32767 ? 16'h7fff : reduced< -32768 ? 16'h8000 : reduced[15:0];
        assign input_clip[g]=(raw>=32760 || raw<= -32760);
        always @(posedge clk) begin
            if(!resetn) begin dc[g]<=0;lp[g]<=0;end
            else if(pcm_valid && !busy) begin
                dc[g]<=dc[g]+(hp>>>5);lp[g]<=next_lp;
            end
        end
    end endgenerate
    // True synchronous read, allowing the frozen result array to infer BRAM.
    always @(posedge clk) selected_corr<=frozen[corr_index];
    assign corr_value=(frozen_valid && corr_index<CORRELATIONS) ?
        {{16{selected_corr[47]}},selected_corr} : 64'd0;
    assign energy_value=frozen_valid ? {16'd0,frozen_energy[energy_index]} : 64'd0;
    assign snapshot_status={29'd0,latest_valid,(pending||copy_active),frozen_valid};
    assign channel_activity=frozen_valid ? {8'd0,frozen_clip,8'd0,
        (frozen_energy[7]!=0),(frozen_energy[6]!=0),(frozen_energy[5]!=0),(frozen_energy[4]!=0),
        (frozen_energy[3]!=0),(frozen_energy[2]!=0),(frozen_energy[1]!=0),(frozen_energy[0]!=0)} : 32'd0;
    integer k;
    always @(posedge clk) begin
        if(!resetn) begin
            wr_ptr<=0;center_ptr<=0;warmup<=0;frame_sample<=0;
            busy<=0;issuing<=0;first_sample<=0;last_sample<=0;
            issue_job<=0;pair_i<=0;pair_j<=1;lag_index<=0;
            valid1<=0;valid2<=0;valid3<=0;job1<=0;job2<=0;job3<=0;
            operand_a<=0;operand_b<=0;product2<=0;old1<=0;old2<=0;sum3<=0;
            latest_valid<=0;latest_sequence<=0;frame_clip<=0;latest_clip<=0;
            overrun_count<=0;sample_count<=0;
        end else begin
            valid1<=issuing;valid2<=valid1;valid3<=valid2;
            if(pcm_valid) begin
                sample_count<=sample_count+1'b1;
                if(busy) overrun_count<=overrun_count+1'b1;
                else begin
                    for(k=0;k<8;k=k+1) history[k*64+wr_ptr]<=conditioned[k*16+:16];
                    wr_ptr<=wr_ptr+1'b1;
                    if(warmup<36) warmup<=warmup+1'b1;
                    else begin
                        center_ptr<=wr_ptr-6'd18;issue_job<=0;pair_i<=0;pair_j<=1;lag_index<=0;
                        first_sample<=(frame_sample==0);last_sample<=(frame_sample==FRAME_SAMPLES-1);
                        frame_clip<=frame_sample==0 ? input_clip : frame_clip|input_clip;
                        busy<=1;issuing<=1;
                    end
                end
            end
            if(issuing) begin
                operand_a<=history[hist_a];operand_b<=history[hist_b];
                old1<=accum[issue_job];job1<=issue_job;
                if(issue_job==JOBS-1) issuing<=0;
                else begin
                    issue_job<=issue_job+1'b1;
                    if(lag_index==36) begin
                        lag_index<=0;
                        if(pair_j==7) begin pair_i<=pair_i+1'b1;pair_j<=pair_i+2'd2;end
                        else pair_j<=pair_j+1'b1;
                    end else lag_index<=lag_index+1'b1;
                end
            end
            if(valid1) begin
                product2<=operand_a*operand_b;old2<=old1;job2<=job1;
            end
            if(valid2) begin
                sum3<=(first_sample ? 48'sd0 : old2)+extended_product;job3<=job2;
            end
            if(valid3) begin
                if(last_sample) begin
                    if(job3<CORRELATIONS) latest[job3]<=sum3;
                    else latest_energy[job3-CORRELATIONS]<=sum3;
                end else accum[job3]<=sum3;
                if(job3==JOBS-1) begin
                    busy<=0;
                    if(last_sample) begin
                        frame_sample<=0;latest_sequence<=latest_sequence+1'b1;
                        latest_valid<=1;latest_clip<=frame_clip;
                    end else frame_sample<=frame_sample+1'b1;
                end
            end
        end
    end
    integer e;
    always @(posedge clk) begin
        if(!resetn) begin
            pending<=0;copy_active<=0;frozen_valid<=0;copy_index<=0;
            copy_destination<=0;copy_read_valid<=0;copy_data<=0;
            frozen_sequence<=0;frozen_clip<=0;
        end else begin
            // Copy starts only between jobs, with at least one non-publication
            // sample before the next frame publication. At 48kHz, 1037 copy
            // clocks fit safely before publication; sampling never waits on copy.
            if(pending && !copy_active && !busy && !pcm_valid && latest_valid &&
               frame_sample!=FRAME_SAMPLES-1) begin
                copy_active<=1;copy_index<=0;copy_read_valid<=0;pending<=0;
                frozen_sequence<=latest_sequence;frozen_clip<=latest_clip;
                for(e=0;e<8;e=e+1) frozen_energy[e]<=latest_energy[e];
            end
            if(copy_active) begin
                if(copy_index<CORRELATIONS) begin
                    copy_data<=latest[copy_index];copy_destination<=copy_index;
                    copy_read_valid<=1;copy_index<=copy_index+1'b1;
                end else copy_read_valid<=0;
                if(copy_read_valid) begin
                    frozen[copy_destination]<=copy_data;
                    if(copy_destination==CORRELATIONS-1) begin
                        copy_active<=0;copy_read_valid<=0;frozen_valid<=1;
                    end
                end
            end
            if(snapshot_request && !pending && !copy_active) begin
                pending<=1;frozen_valid<=0;
            end
            if(snapshot_release) begin
                pending<=0;copy_active<=0;copy_read_valid<=0;frozen_valid<=0;
            end
        end
    end
endmodule
