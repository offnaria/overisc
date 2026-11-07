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
  overisc_pipe_if #(.DATA_W($bits(pcgen_source_t))) ifu_sink ();
  overisc_pipe_if #(.DATA_W($bits(ifu_source_t))) ifu_source ();
  overisc_pipe_if #(.DATA_W($bits(ifu_source_t))) fb_source ();

  // debug
  trace_t trace;
  pcgen_source_t pcgen_data, ifu_sink_data;
  ifu_source_t ifu_source_data, fb_source_data;

  // instances
  overisc_pcgen #(.RESET_VECTOR(RESET_VECTOR)) i_pcgen (.*);

  overisc_skid_buffer #( // This would be merged into IFU in the future, if cache is timing critical.
      .DATA_W($bits(pcgen_source_t))
  ) i_skbuf_pcif (
      .sink  (pcgen_source),
      .source(ifu_sink),
      .*
  );

  overisc_ifu #() i_ifu (.*);

  overisc_skid_buffer #(  // TODO: Replace this to deeper FIFO.
      .DATA_W($bits(ifu_source_t))
  ) i_fb (
      .sink  (ifu_source),
      .source(fb_source),
      .*
  );

  always_comb begin
    // setup


    // logic
    pcgen_data = pcgen_source_t'(pcgen_source.data);
    ifu_sink_data = pcgen_source_t'(ifu_sink.data);
    ifu_source_data = ifu_source_t'(ifu_source.data);
    fb_source_data = ifu_source_t'(fb_source.data);

    trace = '0;
    trace.stage[TRACE_F0] = '{valid: pcgen_source.hs(), id: pcgen_data.id};
    trace.stage[TRACE_F1] = '{valid: ifu_sink.hs(), id: ifu_sink_data.id};
    trace.stage[TRACE_F2] = '{valid: ifu_source.hs(), id: ifu_source_data.id};
    // TODO: Report TRACE_FB once the Fetch Buffer is a FIFO.
    trace.stage[TRACE_D] = '{valid: fb_source.hs(), id: fb_source_data.id};
    trace.f0_pc = pcgen_data.pc;
    trace.f2_inst = ifu_source_data.inst;

    // output
    dbus_req = '{default: '0};
    io_req = '{default: '0};
    commit = '{default: '0};
  end

  // TODO: Remove this block once implementation completed.
  always_comb begin
    fb_source.ready = 1'b1;
    if (0) begin
      $write("data=%d valid=%d ready=%d\n", fb_source.data, fb_source.valid, fb_source.ready);
    end
  end

endmodule
