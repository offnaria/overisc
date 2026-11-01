// SPDX-License-Identifier: CERN-OHL-P-2.0
// Instruction Fetch Unit of Overisc core.

module overisc_ifu
  import overisc_cfg_pkg::*, overisc_axi_pkg::*, overisc_pipe_pkg::*;
#(

) (
    input clk,
    input rst,

    // Instruction bus
    output axi_req_t  ibus_req,
    input  axi_resp_t ibus_resp,

    overisc_pipe_if.sink   ifu_sink,
    overisc_pipe_if.source ifu_source
);

  typedef enum logic [1:0] {
    IDLE,
    FETCH
  } state_e;

  state_e state, state_w;

  // ibus
  logic ibus_arvalid, ibus_arvalid_w;
  axi_addr_t ibus_araddr, ibus_araddr_w;
  logic ibus_rready, ibus_rready_w;
  axi_data_t ibus_rdata, ibus_rdata_w;

  // pipe
  pcgen_source_t sink_data, sink_data_w;
  logic sink_ready, sink_ready_w;

  ifu_source_t source_data;
  logic source_valid;

  always_comb begin
    // setup
    state_w = state;
    ibus_arvalid_w = ibus_arvalid;
    ibus_araddr_w = ibus_araddr;
    ibus_rready_w = ibus_rready;
    ibus_rdata_w = ibus_rdata;
    sink_ready_w = sink_ready;
    sink_data_w = sink_data;

    // logic
    source_valid = '0;
    // TODO: Add cache interface.
    // TODO: Add address translation logic.
    case (state)
      IDLE: begin
        sink_data_w   = pcgen_source_t'(ifu_sink.data);
        ibus_araddr_w = axi_addr_t'(sink_data_w.pc);
        if (sink_ready && ifu_sink.valid) begin
          ibus_arvalid_w = 1'b1;
          ibus_rready_w = 1'b1;
          state_w = FETCH;
        end
      end
      FETCH: begin
        if (ibus_resp.ar_ready) begin
          ibus_arvalid_w = '0;
        end
        if (ibus_resp.r_valid) begin
          ibus_rready_w = '0;
          ibus_rdata_w  = ibus_resp.r.data;
        end
        if (!ibus_rready_w) begin
          source_valid = 1'b1;
          if (ifu_source.ready) begin
            state_w = IDLE;
          end
        end
      end
      default: ;
    endcase
    source_data = '{
        inst: ibus_rdata_w[ILEN*sink_data.pc[3:2]+:ILEN],  // TODO: Support C-extension.
        pc: sink_data.pc,
        untaken_pc: sink_data.untaken_pc,
        id: sink_data.id
    };
    sink_ready_w = (state_w == IDLE);

    // output
    ifu_sink.ready = sink_ready;
    ifu_source.data = source_data;
    ifu_source.valid = source_valid;
    ibus_req = '{
        default: '0,
        ar: '{
            default: '0,
            addr: ibus_araddr,
            burst: AXI_BURST_INCR,  // correct?
            size: 2  // correct?
        },
        ar_valid: ibus_arvalid,
        r_ready: ibus_rready
    };
  end

  always_ff @(posedge clk) begin
    state <= state_w;
    ibus_arvalid <= ibus_arvalid_w;
    ibus_araddr <= ibus_araddr_w;
    ibus_rready <= ibus_rready_w;
    ibus_rdata <= ibus_rdata_w;
    sink_data <= sink_data_w;
    sink_ready <= sink_ready_w;

    if (rst) begin
      state <= IDLE;
      ibus_arvalid <= '0;
      ibus_rready <= '0;
      sink_ready <= '0;
    end

    if (0) begin
      $write("ibus_req=0x%x ibus_resp=0x%x\n", ibus_req, ibus_resp);
    end
  end

endmodule
