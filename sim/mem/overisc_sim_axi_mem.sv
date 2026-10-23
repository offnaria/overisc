// SPDX-License-Identifier: Apache-2.0
// AXI4 slave memory for simulation (B2).
// Protocol handling, randomized timing and storage live in C++ (axi_slave.cc); this wrapper
// samples the master outputs at each clock edge and registers the slave outputs, so the
// slave outputs never depend combinationally on the master.
// All instances share one memory; the testbench must call overisc_sim_init() at time 0.
module overisc_sim_axi_mem
  import overisc_axi_pkg::*;
#(
    parameter string NAME = "axi"
) (
    input  logic      clk,
    input  logic      rst,
    input  axi_req_t  req,
    output axi_resp_t resp
);

  // The DPI data ports are fixed at 128 bits (kAxiMaxDataBytes in axi_slave.h).
  localparam int DpiDataW = 128;
  if (AXI_DATA_W > DpiDataW) begin : gen_width_check
    $error("overisc_sim_axi_mem: AXI_DATA_W must be <= %0d", DpiDataW);
  end

  import "DPI-C" function int overisc_sim_axi_new(
    input string name,
    input int    data_bytes
  );
  import "DPI-C" function void overisc_sim_axi_reset(input int handle);
  import "DPI-C" function int overisc_sim_axi_step(
    input int handle,
    input bit aw_valid,
    input int aw_id,
    input longint aw_addr,
    input int aw_len,
    input int aw_size,
    input int aw_burst,
    input bit w_valid,
    input bit [DpiDataW-1:0] w_data,
    input int w_strb,
    input bit w_last,
    input bit b_ready,
    input bit ar_valid,
    input int ar_id,
    input longint ar_addr,
    input int ar_len,
    input int ar_size,
    input int ar_burst,
    input bit r_ready,
    output bit aw_ready,
    output bit w_ready,
    output bit ar_ready,
    output bit b_valid,
    output int b_id,
    output int b_resp,
    output bit r_valid,
    output int r_id,
    output bit [DpiDataW-1:0] r_data,
    output int r_resp,
    output bit r_last
  );

  int handle;
  initial handle = overisc_sim_axi_new(NAME, AXI_DATA_W / 8);

  bit aw_ready, w_ready, ar_ready, b_valid, r_valid, r_last;
  // DPI returns full ints; only the low bits are meaningful
  /* verilator lint_off UNUSEDSIGNAL */
  int b_id, b_resp, r_id, r_resp;
  /* verilator lint_on UNUSEDSIGNAL */
  bit [DpiDataW-1:0] r_data;

  always @(posedge clk) begin
    if (rst) begin
      overisc_sim_axi_reset(handle);
      resp <= '0;
    end else begin
      if (overisc_sim_axi_step(
              handle,
              req.aw_valid,
              int'(req.aw.id),
              longint'(req.aw.addr),
              int'(req.aw.len),
              int'(req.aw.size),
              int'(req.aw.burst),
              req.w_valid,
              DpiDataW'(req.w.data),
              int'(req.w.strb),
              req.w.last,
              req.b_ready,
              req.ar_valid,
              int'(req.ar.id),
              longint'(req.ar.addr),
              int'(req.ar.len),
              int'(req.ar.size),
              int'(req.ar.burst),
              req.r_ready,
              aw_ready,
              w_ready,
              ar_ready,
              b_valid,
              b_id,
              b_resp,
              r_valid,
              r_id,
              r_data,
              r_resp,
              r_last
          ) != 0) begin
        $fatal(1, "%s: AXI protocol violation (see message above)", NAME);
      end
      resp.aw_ready <= aw_ready;
      resp.w_ready  <= w_ready;
      resp.ar_ready <= ar_ready;
      resp.b_valid  <= b_valid;
      resp.b.id     <= axi_id_t'(b_id);
      resp.b.resp   <= axi_resp_code_t'(b_resp[1:0]);
      resp.b.user   <= '0;
      resp.r_valid  <= r_valid;
      resp.r.id     <= axi_id_t'(r_id);
      resp.r.data   <= axi_data_t'(r_data);
      resp.r.resp   <= axi_resp_code_t'(r_resp[1:0]);
      resp.r.last   <= r_last;
      resp.r.user   <= '0;
    end
  end

endmodule
