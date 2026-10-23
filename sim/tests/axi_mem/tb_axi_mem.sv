// SPDX-License-Identifier: Apache-2.0
// Smoke test of the SV/DPI glue of the simulation memory (B9).
// Writes random INCR bursts through the D-side port, reads them back through the I-side port
// (shared memory), and checks DECERR for unmapped addresses and the MMIO terminator.
// The protocol logic itself is covered by test_axi_slave.cc.
//
// plusargs: +seed=<n> +mem_profile=<fast|small|large|random> +iters=<n>
//
// The master is procedural, so it drives and samples on the falling edge, where both sides
// are stable: a handshake seen at a falling edge (valid && ready) completes at the next
// rising edge. This avoids depending on how the simulator orders a resumed task against the
// clocked always blocks of the slaves.
module tb_axi_mem;
  import overisc_axi_pkg::*;

  localparam longint DramBase = 64'h8000_0000;
  localparam longint DramSize = 64'h0010_0000;
  localparam int BusBytes = AXI_DATA_W / 8;
  localparam longint BusBytesL = longint'(BusBytes);

  import "DPI-C" function int overisc_sim_init(
    input longint seed,
    input string  profile,
    input longint dram_base,
    input longint dram_size
  );
  import "DPI-C" function longint overisc_sim_mem_read64(input longint addr);

  logic clk = 1'b0;
  logic rst = 1'b1;
  always #5 clk <= ~clk;

  axi_req_t ireq, dreq;
  // id/user/resp bits that the test does not look at
  /* verilator lint_off UNUSEDSIGNAL */
  axi_resp_t iresp, dresp;
  axil_req_t  lreq;
  axil_resp_t lresp;
  /* verilator lint_on UNUSEDSIGNAL */

  overisc_sim_axi_mem #(
      .NAME("ibus")
  ) u_imem (
      .clk (clk),
      .rst (rst),
      .req (ireq),
      .resp(iresp)
  );
  overisc_sim_axi_mem #(
      .NAME("dbus")
  ) u_dmem (
      .clk (clk),
      .rst (rst),
      .req (dreq),
      .resp(dresp)
  );
  overisc_sim_axil_err #(
      .NAME("iobus")
  ) u_io (
      .clk (clk),
      .rst (rst),
      .req (lreq),
      .resp(lresp)
  );

  byte unsigned shadow[longint];
  int errors = 0;

  function automatic byte unsigned expect_byte(longint a);
    return shadow.exists(a) != 0 ? shadow[a] : 8'h00;
  endfunction

  // Active byte lanes of beat i of an INCR burst.
  function automatic logic [BusBytes-1:0] lanes(longint addr, int size, int i);
    longint nbytes = 64'(1) << size;
    longint a = (i == 0) ? addr : (addr & ~(nbytes - 1)) + i * nbytes;
    int lo = int'(a % BusBytesL);
    int hi = int'((a & ~(nbytes - 1)) % BusBytesL + nbytes - 1);
    logic [BusBytes-1:0] m = '0;
    for (int j = lo; j <= hi; j++) m[j] = 1'b1;
    return m;
  endfunction

  function automatic longint beat_block(longint addr, int size, int i);
    longint nbytes = 64'(1) << size;
    longint a = (i == 0) ? addr : (addr & ~(nbytes - 1)) + i * nbytes;
    return a & ~(BusBytesL - 1);
  endfunction

  // Called at a falling edge. Returns at the falling edge after the handshake.
  task automatic wait_cycle();
    @(posedge clk);
    @(negedge clk);
  endtask

  task automatic dbus_write(input int id, input longint addr, input int len, input int size,
                            output int bresp);
    dreq.aw.id    = axi_id_t'(id);
    dreq.aw.addr  = axi_addr_t'(addr);
    dreq.aw.len   = axi_len_t'(len);
    dreq.aw.size  = axi_size_t'(size);
    dreq.aw.burst = AXI_BURST_INCR;
    dreq.aw_valid = 1'b1;
    while (!dresp.aw_ready) wait_cycle();
    wait_cycle();
    dreq.aw_valid = 1'b0;
    for (int i = 0; i <= len; i++) begin
      axi_data_t d;
      logic [BusBytes-1:0] m;
      for (int j = 0; j < BusBytes; j++) d[8*j+:8] = 8'($urandom);
      m = lanes(addr, size, i) & BusBytes'($urandom);
      for (int j = 0; j < BusBytes; j++) begin
        if (m[j] && addr >= DramBase) shadow[beat_block(addr, size, i)+longint'(j)] = d[8*j+:8];
      end
      dreq.w.data  = d;
      dreq.w.strb  = m;
      dreq.w.last  = (i == len);
      dreq.w_valid = 1'b1;
      while (!dresp.w_ready) wait_cycle();
      wait_cycle();
      dreq.w_valid = 1'b0;
    end
    dreq.b_ready = 1'b1;
    while (!dresp.b_valid) wait_cycle();
    if (int'(dresp.b.id) != id) begin
      $display("ERROR: BID %0d, expected %0d", dresp.b.id, id);
      errors++;
    end
    bresp = int'(dresp.b.resp);
    wait_cycle();
    dreq.b_ready = 1'b0;
  endtask

  task automatic ibus_read_check(input int id, input longint addr, input int len, input int size,
                                 input bit expect_err);
    ireq.ar.id    = axi_id_t'(id);
    ireq.ar.addr  = axi_addr_t'(addr);
    ireq.ar.len   = axi_len_t'(len);
    ireq.ar.size  = axi_size_t'(size);
    ireq.ar.burst = AXI_BURST_INCR;
    ireq.ar_valid = 1'b1;
    while (!iresp.ar_ready) wait_cycle();
    wait_cycle();
    ireq.ar_valid = 1'b0;
    ireq.r_ready  = 1'b1;
    for (int i = 0; i <= len; i++) begin
      logic [BusBytes-1:0] m;
      while (!iresp.r_valid) wait_cycle();
      m = lanes(addr, size, i);
      if (iresp.r.last != (i == len) || int'(iresp.r.id) != id) begin
        $display("ERROR: RLAST/RID wrong on beat %0d of 0x%0h", i, addr);
        errors++;
      end
      if (expect_err) begin
        if (iresp.r.resp != AXI_RESP_DECERR) begin
          $display("ERROR: expected DECERR for 0x%0h", addr);
          errors++;
        end
      end else begin
        for (int j = 0; j < BusBytes; j++) begin
          if (m[j] && iresp.r.data[8*j+:8] != expect_byte(
                  beat_block(addr, size, i) + longint'(j)
              )) begin
            $display("ERROR: 0x%0h beat %0d lane %0d: got %02h expected %02h", addr, i, j,
                     iresp.r.data[8*j+:8], expect_byte(beat_block(addr, size, i) + longint'(j)));
            errors++;
          end
        end
      end
      wait_cycle();
    end
    ireq.r_ready = 1'b0;
  endtask

  initial begin
    longint seed = 1;
    string profile = "small";
    int iters = 200;
    int bresp;

    void'($value$plusargs("seed=%d", seed));
    void'($value$plusargs("mem_profile=%s", profile));
    void'($value$plusargs("iters=%d", iters));
    if (overisc_sim_init(seed, profile, DramBase, DramSize) != 0) $fatal(1, "init failed");
    void'($urandom(int'(seed)));

    ireq = '0;
    dreq = '0;
    lreq = '0;
    repeat (3) @(posedge clk);
    @(negedge clk);
    rst = 1'b0;
    wait_cycle();

    for (int n = 0; n < iters; n++) begin
      automatic int size = $urandom_range(4);
      automatic int len = $urandom_range(7);
      automatic longint nbytes = 64'(1) << size;
      automatic longint addr = DramBase + longint'($urandom_range(int'(DramSize) - 4096));
      automatic int id = $urandom_range(3);
      // keep the burst inside a 4KB page
      if (((addr & ~(nbytes - 1)) + (longint'(len) + 1) * nbytes - 1) >> 12 != addr >> 12)
        addr = addr & ~longint'(4095);
      dbus_write(id, addr, len, size, bresp);
      if (bresp != int'(AXI_RESP_OKAY)) begin
        $display("ERROR: BRESP %0d for 0x%0h", bresp, addr);
        errors++;
      end
      ibus_read_check(id, addr, len, size, 1'b0);
    end

    // unmapped address
    dbus_write(0, 64'h4000_0000, 1, 3, bresp);
    if (bresp != int'(AXI_RESP_DECERR)) begin
      $display("ERROR: write to unmapped address returned %0d", bresp);
      errors++;
    end
    ibus_read_check(1, 64'h4000_0000, 1, 3, 1'b1);

    // backdoor agrees with the bus
    begin
      longint a = DramBase + 64'h100;
      longint v = 0;
      for (int j = 7; j >= 0; j--) v = (v << 8) | longint'(expect_byte(a + longint'(j)));
      if (overisc_sim_mem_read64(a) != v) begin
        $display("ERROR: backdoor read mismatch");
        errors++;
      end
    end

    // MMIO terminator answers DECERR
    lreq.ar.addr  = axil_addr_t'(64'h1000_0000);
    lreq.ar_valid = 1'b1;
    lreq.r_ready  = 1'b1;
    while (!lresp.ar_ready) wait_cycle();
    wait_cycle();
    lreq.ar_valid = 1'b0;
    while (!lresp.r_valid) wait_cycle();
    if (lresp.r.resp != AXI_RESP_DECERR) begin
      $display("ERROR: MMIO read did not return DECERR");
      errors++;
    end

    if (errors == 0) $display("PASS seed=%0d profile=%s iters=%0d", seed, profile, iters);
    else $display("FAIL: %0d errors", errors);
    $finish;
  end

endmodule
