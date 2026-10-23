// SPDX-License-Identifier: Apache-2.0
// Runs the SV/DPI smoke test of the simulation memory (tb_axi_mem.sv) for each timing profile
// and a few seeds.
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <string>

#include "Vtb_axi_mem.h"
#include "sim_run.h"

TEST_CASE("AXI memory SV/DPI glue", "[axi][verilator]") {
  const std::string profile = GENERATE("fast", "small", "large", "random");
  const int seed = GENERATE(range(1, 6));
  CAPTURE(profile, seed);
  SimRunOptions opt;
  opt.plusargs = {"+seed=" + std::to_string(seed), "+mem_profile=" + profile};
  const SimResult r = run_timed<Vtb_axi_mem>(opt);
  INFO("errors=" << r.value);
  CHECK(r.kind == SimResultKind::kPass);
}
