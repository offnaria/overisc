// SPDX-License-Identifier: CERN-OHL-P-2.0
// Top module of Overisc core.

/* verilator lint_off UNUSEDSIGNAL */
module overisc_core_top
  import overisc_cfg_pkg::*, overisc_axi_pkg::*, overisc_commit_pkg::*, overisc_pipe_pkg::*, overisc_trace_pkg::*;
#(
    parameter logic [XLEN-1:0] RESET_VECTOR = '0
) (
    input clk,
    input rst,

    // Instruction bus
    output axi_req_t  ibus_req,
    input  axi_resp_t ibus_resp,

    // Data bus (cached)
    output axi_req_t  dbus_req,
    input  axi_resp_t dbus_resp,

    // IO bus (uncached)
    output axil_req_t  io_req,
    input  axil_resp_t io_resp,

    // Commit record
    output commit_t [NRET-1:0] commit
);

  // internal interface
  overisc_pipe_if #(.DATA_W($bits(pcgen_source_t))) pcgen_source ();
  overisc_pipe_if #(.DATA_W($bits(ifu_source_t))) ifu_source ();
  overisc_pipe_if #(.DATA_W($bits(ifu_source_t))) predec_sink ();
  overisc_pipe_if #(.DATA_W($bits(predec_source_t))) predec_source ();
  overisc_pipe_if #(.DATA_W($bits(predec_source_t))) fb_source ();

  // debug
  trace_t trace;
  logic [XLEN-1:0] pcgen_npc;
  trace_id_t pcgen_nid;
  pcgen_source_t pcgen_data;
  ifu_source_t ifu_data;
  predec_source_t prdec_data, fb_data;
  trace_id_t fb_trace_id;
  logic fb_trace_valid;

  // instances
  overisc_pcgen #(.RESET_VECTOR(RESET_VECTOR)) i_pcgen (.*);

  overisc_ifu #() i_ifu (
      .ifu_sink(pcgen_source),
      .*
  );

  overisc_skid_buffer #(  // This would be merged into IFU in the future.
      .DATA_W($bits(ifu_source_t))
  ) i_skbuf_ifpd (
      .sink  (ifu_source),
      .source(predec_sink),
      .*
  );

  overisc_predec #() i_predec (.*);

  overisc_fetch_buffer #(
      .DATA_T (predec_source_t),
      .DEPTH_W(4)
  ) i_fb (
      .sink  (predec_source),
      .source(fb_source),
      .*
  );

  always_comb begin
    // setup


    // logic
    pcgen_data = pcgen_source_t'(pcgen_source.data);
    ifu_data = ifu_source_t'(ifu_source.data);
    prdec_data = predec_source_t'(predec_source.data);
    fb_data = predec_source_t'(fb_source.data);

    trace = '0;
    trace.stage[TRACE_F0] = '{valid: 1'b1, id: pcgen_nid};  // Keep asserting valid is allowed.
    trace.stage[TRACE_F1] = '{valid: pcgen_source.hs(), id: pcgen_data.id};
    trace.stage[TRACE_F2] = '{valid: ifu_source.hs(), id: ifu_data.id};
    trace.stage[TRACE_FB] = '{valid: predec_source.hs(), id: prdec_data.id};
    trace.stage[TRACE_D] = '{valid: fb_trace_valid, id: fb_trace_id};
    trace.f0_pc = pcgen_npc;
    trace.f2_inst = ifu_data.inst;
    trace.retire[0] = '{valid: fb_source.hs(), id: fb_data.id};  // TODO

    // output
    dbus_req = '{default: '0};
    io_req = '{default: '0};
    commit = '{default: '0};
  end

  // TODO: Remove this block once implementation completed.
  logic [31:0] cnt, cnt_w;
  always_comb begin
    cnt_w = (cnt == '0) ? 'd7 : (cnt - 'd1);
    fb_source.ready = (cnt == '0);
    if (0) begin
      $write("data=%d valid=%d ready=%d\n", fb_source.data, fb_source.valid, fb_source.ready);
    end
  end

  always_ff @(posedge clk) begin
    cnt <= cnt_w;

    if (rst) begin
      cnt <= 'd7;
    end
  end

endmodule
