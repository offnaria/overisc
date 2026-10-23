// SPDX-License-Identifier: CERN-OHL-P-2.0
// Top module of Overisc core.

/* verilator lint_off UNUSEDSIGNAL */
module overisc_core_top
  import overisc_cfg_pkg::*, overisc_axi_pkg::*, overisc_commit_pkg::*;
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

  logic [XLEN-1:0] pc, pc_w;

  // ibus
  logic ibus_arvalid, ibus_arvalid_w;
  logic ibus_rready, ibus_rready_w;

  always_comb begin
    // setup
    pc_w = pc;
    ibus_arvalid_w = ibus_arvalid;
    ibus_rready_w = ibus_rready;

    // logic
    if (ibus_arvalid) begin
      if (ibus_resp.ar_ready) begin
        ibus_arvalid_w = '0;
        ibus_rready_w  = 1'b1;
      end
    end
    if (ibus_rready) begin
      if (ibus_resp.r_valid) begin
        ibus_rready_w = '0;
        ibus_arvalid_w = 1'b1;
        pc_w = pc + 'd4;
      end
    end

    // output
    ibus_req = '{
        default: '0,
        ar: '{
            default: '0,
            addr: axi_addr_t'(pc),
            burst: AXI_BURST_WRAP,  // correct?
            size: 2  // correct?
        },
        ar_valid : !rst && ibus_arvalid,
        r_ready: ibus_rready
    };
    dbus_req = '{default: '0};
    io_req = '{default: '0};
    commit = '{default: '0};
  end

  always_ff @(posedge clk) begin
    pc <= pc_w;
    ibus_arvalid <= ibus_arvalid_w;
    ibus_rready <= ibus_rready_w;

    if (rst) begin
      pc <= RESET_VECTOR;
      ibus_arvalid <= 1'b1;
      ibus_rready <= '0;
    end
  end

endmodule
