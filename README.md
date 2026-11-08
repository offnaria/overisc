# Overisc

Overisc is offNaria's ~~(n+1)-th~~ second RISC-V processor system.

Design notes (Japanese): [docs/design.md](docs/design.md), decisions: [docs/decisions.md](docs/decisions.md),
current tasks: [docs/m0-tasks.md](docs/m0-tasks.md).

## Requirements

| Tool | Version used | Notes |
|---|---|---|
| Verilator | 5.054 | needs `liblz4-dev` for FST waveforms |
| CMake | 3.22 or later | |
| g++ | 11 or later (C++20) | |
| Verible | v0.0-4007 | `verible-verilog-format` |
| autoconf, dtc | | for building Spike |
| RISC-V GCC | 16.2.0 | fetched by `make toolchain` into `.tools/` |

## First-time setup

```sh
git submodule update --init --recursive
make toolchain      # kernel.org crosstool GCC into .tools/
make spike          # Spike into build/spike/install
make sim            # builds riscv-tests (build/riscv-tests) and all simulators (build/sim)
```

## Everyday commands

```sh
make lint           # Verilator lint of the core (-Wall, must be warning-free)
make format         # format SystemVerilog with Verible (default settings)
make format-check   # check formatting only
make test           # build everything and run all tests in parallel (CTest)
```

`make test` passes `CTEST_ARGS` to CTest, e.g. `make test CTEST_ARGS="-L unit"`.

## Choosing tests

Tests are registered in CTest with labels:

| Label | Contents |
|---|---|
| `unit` | C++ unit tests (AXI slave model, ELF loader) |
| `verilator` | Verilated tests of the simulation models (memory SV/DPI glue, host/tohost) |
| `riscv-tests` | the core running riscv-tests, one test per program x seed x memory profile |

```sh
ctest --test-dir build/sim -N                      # list tests
ctest --test-dir build/sim -j$(nproc) -L unit      # by label
ctest --test-dir build/sim -j$(nproc) -LE riscv-tests  # everything except the core
ctest --test-dir build/sim -R rv64ui-p-add         # by name (regex)
ctest --test-dir build/sim -R 'fast/.*/rv64um'     # one profile, one suite
ctest --test-dir build/sim --rerun-failed --output-on-failure
```

Core test names are `riscv-tests/<profile>/<seed>/<program>`. The seeds and profiles are CMake
cache variables:

```sh
cmake build/sim -DREGRESSION_SEEDS="1;2;3" -DREGRESSION_PROFILES="fast;large"
```

Rebuild after changing RTL with `make sim` (or `cmake --build build/sim -j`).

## Running one program on the core

`make run` builds what is needed and runs one program, writing a Konata log by default:

```sh
make run ELF=build/riscv-tests/rv64ui-p-add                      # Konata log on
make run ELF=build/riscv-tests/rv64ui-p-add SEED=3 PROFILE=large FST=add.fst
make run ELF=<long program> KONATA=0 MAX_CYCLES=100000000        # e.g. Linux: no Konata log
```

Or call the simulator directly (nothing is written unless asked):

```sh
build/sim/test_core --elf build/riscv-tests/rv64ui-p-add
build/sim/test_core --elf build/riscv-tests/rv64ui-p-add --seed 3 --profile large \
                    --max-cycles 200000 --fst add.fst
gtkwave add.fst
```

| Option | Default | Meaning |
|---|---|---|
| `--elf <path>` | (required) | program to load; its entry point must be the reset vector (0x80000000) |
| `--seed <n>` | 1 | seed for the memory timing and for the initial value of non-reset state |
| `--profile <name>` | fast | memory timing: `fast` (always ready, in order), `small`, `large` (long latency, reordering, interleaving), `random` (parameters drawn from the seed) |
| `--max-cycles <n>` | 1000000 | cycle limit after reset |
| `--fst <file>` | | write an FST waveform (also `OVERISC_FST=<file>` in the environment) |
| `--konata <file>` | | write a pipeline log for [Konata](https://github.com/shioyadan/Konata) (see below) |

The result line is one of:

| Result | Meaning |
|---|---|
| `PASS` | the program wrote 1 to `tohost` |
| `FAIL ... test N` | the program wrote `(N << 1) \| 1` to `tohost`; N is the failing test case number in the riscv-tests source |
| `TIMEOUT` | no write to `tohost` within the cycle limit |
| `ERROR` | setup failure, or an AXI protocol violation by the core (the message above names the port and the rule) |

Disassembly of each test is next to the ELF: `build/riscv-tests/<program>.dump`.

### Pipeline view with Konata

The core exposes an internal `trace` signal (`rtl/core/overisc_trace_pkg.sv`); the testbench turns
it into a Kanata log when `test_core` gets `--konata <file>`. The callers turn it on by default:

| Caller | Log |
|---|---|
| `make run ELF=...` | `build/sim/konata/<program>_<profile>_s<seed>.log` (`KONATA=0` to skip) |
| CTest regression | same path per test (CMake option `OVERISC_KONATA_REGRESSION=OFF` to skip; CI skips) |

A file is overwritten by the next run of the same test. A run that reaches the cycle limit writes
about 30 MB per million cycles, so skip the log for long programs such as Linux. The CMake option
`OVERISC_KONATA=OFF` removes the hook altogether. CMake options can be passed through
`make sim CMAKE_ARGS=...`.

Open the log in Konata. Stages are `F0 F1 F2 FB D X M W`. Labels show the PC and, once the
instruction reached F2, its disassembly (Spike's disassembler if Spike is built with `make spike`,
otherwise the encoding); wrong-path instructions flushed before F2 show only the PC.

## License

Hardware (RTL, synthesis scripts, documentation) is licensed under CERN-OHL-P-2.0,
and software (simulation, test programs, build scripts) under Apache-2.0.
See [LICENSE](LICENSE) for details.
