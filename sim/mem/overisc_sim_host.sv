// SPDX-License-Identifier: Apache-2.0
// Host side of a simulation run (B3): sets up the shared memory, loads the ELF, watches
// tohost and enforces a cycle limit.
//
// tohost protocol (riscv-tests): the program writes (test_number << 1) | 1 to tohost;
// 1 means pass.
//
// Prints exactly one result line starting with PASS, FAIL, TIMEOUT or ERROR and calls
// $finish, so scripts decide by that line.
//
// plusargs:
//   +elf=<path>            program to load (required)
//   +seed=<n>              random seed of the memory timing (default 1)
//   +mem_profile=<name>    fast | small | large | random (default fast)
//   +max_cycles=<n>        cycle limit after reset (default 1000000)
module overisc_sim_host #(
    parameter longint DRAM_BASE = 64'h8000_0000,
    parameter longint DRAM_SIZE = 64'h0800_0000,
    // must match the core's reset vector; checked against the ELF entry point
    parameter longint RESET_VECTOR = 64'h8000_0000
) (
    input logic clk,
    input logic rst
);

  import "DPI-C" function int overisc_sim_init(
    input longint seed,
    input string  profile,
    input longint dram_base,
    input longint dram_size
  );
  import "DPI-C" function int overisc_sim_load_elf(
    input  string  path,
    output longint entry,
    output longint tohost
  );
  import "DPI-C" function longint overisc_sim_mem_read64(input longint addr);

  string elf;
  longint tohost_addr;
  longint max_cycles;
  longint cycles;
  bit done;

  initial begin
    longint seed = 1;
    string  profile = "fast";
    longint entry;

    max_cycles = 1_000_000;
    cycles = 0;
    done = 1'b0;
    void'($value$plusargs("seed=%d", seed));
    void'($value$plusargs("mem_profile=%s", profile));
    void'($value$plusargs("max_cycles=%d", max_cycles));
    if (!$value$plusargs("elf=%s", elf)) begin
      $display("ERROR: +elf=<path> is required");
      $finish;
    end else if (overisc_sim_init(seed, profile, DRAM_BASE, DRAM_SIZE) != 0) begin
      $display("ERROR: unknown +mem_profile=%s", profile);
      $finish;
    end else if (overisc_sim_load_elf(elf, entry, tohost_addr) != 0) begin
      $display("ERROR: cannot load %s", elf);
      $finish;
    end else if (entry != RESET_VECTOR) begin
      $display("ERROR: %s: entry point 0x%0h differs from the reset vector 0x%0h", elf, entry,
               RESET_VECTOR);
      $finish;
    end else if (tohost_addr < 0) begin
      $display("ERROR: %s: no tohost symbol", elf);
      $finish;
    end else begin
      $display("[host] elf=%s seed=%0d mem_profile=%s tohost=0x%0h", elf, seed, profile,
               tohost_addr);
    end
  end

  always @(posedge clk) begin
    if (!rst && !done) begin
      automatic longint v = overisc_sim_mem_read64(tohost_addr);
      cycles <= cycles + 1;
      if (v != 0) begin
        done <= 1'b1;
        if (v == 1) $display("PASS %s (%0d cycles)", elf, cycles + 1);
        else $display("FAIL %s: test %0d (tohost=0x%0h, %0d cycles)", elf, v >>> 1, v, cycles + 1);
        $finish;
      end else if (cycles + 1 >= max_cycles) begin
        done <= 1'b1;
        $display("TIMEOUT %s: no write to tohost in %0d cycles", elf, max_cycles);
        $finish;
      end
    end
  end

endmodule
