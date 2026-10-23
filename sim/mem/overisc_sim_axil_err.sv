// SPDX-License-Identifier: Apache-2.0
// AXI4-Lite slave that answers every access with DECERR.
// Terminates the D-side MMIO port until the SoC exists (M2). Accesses are reported, since
// the core should not issue any in M0.
module overisc_sim_axil_err
  import overisc_axi_pkg::*;
#(
    parameter string NAME = "axil"
) (
    input  logic       clk,
    input  logic       rst,
    /* verilator lint_off UNUSEDSIGNAL */
    input  axil_req_t  req,  // data, strobes and prot are ignored
    /* verilator lint_on UNUSEDSIGNAL */
    output axil_resp_t resp
);

  logic aw_ready_q, w_ready_q, b_valid_q, ar_ready_q, r_valid_q;

  // Write: accept AW and W in either order, respond once both have arrived, then accept the
  // next pair after B is taken. Read: one outstanding.
  always_ff @(posedge clk) begin
    if (rst) begin
      aw_ready_q <= 1'b1;
      w_ready_q  <= 1'b1;
      b_valid_q  <= 1'b0;
      ar_ready_q <= 1'b1;
      r_valid_q  <= 1'b0;
    end else begin
      if (req.aw_valid && aw_ready_q) begin
        aw_ready_q <= 1'b0;
        $display("%s: write to 0x%0h -> DECERR", NAME, req.aw.addr);
      end
      if (req.w_valid && w_ready_q) w_ready_q <= 1'b0;
      if (!aw_ready_q && !w_ready_q && !b_valid_q) b_valid_q <= 1'b1;
      if (b_valid_q && req.b_ready) begin
        b_valid_q  <= 1'b0;
        aw_ready_q <= 1'b1;
        w_ready_q  <= 1'b1;
      end

      if (req.ar_valid && ar_ready_q) begin
        ar_ready_q <= 1'b0;
        r_valid_q  <= 1'b1;
        $display("%s: read from 0x%0h -> DECERR", NAME, req.ar.addr);
      end
      if (r_valid_q && req.r_ready) begin
        r_valid_q  <= 1'b0;
        ar_ready_q <= 1'b1;
      end
    end
  end

  always_comb begin
    resp          = '0;
    resp.aw_ready = aw_ready_q;
    resp.w_ready  = w_ready_q;
    resp.b_valid  = b_valid_q;
    resp.b.resp   = AXI_RESP_DECERR;
    resp.ar_ready = ar_ready_q;
    resp.r_valid  = r_valid_q;
    resp.r.resp   = AXI_RESP_DECERR;
  end

endmodule
