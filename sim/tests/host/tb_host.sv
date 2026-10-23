// SPDX-License-Identifier: Apache-2.0
// Test of overisc_sim_host (B3) without a core: a fake program writes tohost through the
// memory backdoor.
//
// plusargs (besides those of overisc_sim_host):
//   +fake_tohost=<value>   value to write (0: never write, expect TIMEOUT)
//   +fake_at=<cycle>       cycle of the write (default 100)
module tb_host;

  import "DPI-C" function void overisc_sim_mem_write64(
    input longint addr,
    input longint data
  );

  logic clk = 1'b0;
  logic rst = 1'b1;
  always #5 clk <= ~clk;

  overisc_sim_host u_host (
      .clk(clk),
      .rst(rst)
  );

  initial begin
    longint value = 0;
    int at = 100;
    void'($value$plusargs("fake_tohost=%d", value));
    void'($value$plusargs("fake_at=%d", at));
    repeat (3) @(posedge clk);
    @(negedge clk);
    rst = 1'b0;
    if (value != 0) begin
      repeat (at) @(negedge clk);
      overisc_sim_mem_write64(u_host.tohost_addr, value);
    end
  end

endmodule
