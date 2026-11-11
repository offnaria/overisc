# SPDX-License-Identifier: Apache-2.0
# シミュレーションと試験のビルド（CMake + CTest + Catch2 + Verilator、D-37）
#
# 通常はトップの Makefile から使う（make sim / make test）。直接使う場合:
#   cmake -S sim -B build/sim -DRVTESTS_DIR=... -DRISCV_PREFIX=...
#   cmake --build build/sim -j
#   ctest --test-dir build/sim -j
cmake_minimum_required(VERSION 3.22)
project(overisc_sim CXX)

set(CMAKE_CXX_STANDARD 20)  # Verilator --timing uses coroutines
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(OVERISC_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(RVTESTS_DIR ${OVERISC_ROOT}/build/riscv-tests CACHE PATH "riscv-tests build directory")
set(RISCV_PREFIX "riscv64-linux-" CACHE STRING "RISC-V toolchain prefix (may include a path)")
set(SPIKE_HOME ${OVERISC_ROOT}/build/spike/install CACHE PATH "Spike install directory (make spike)")
option(OVERISC_KONATA "Hook the Kanata logger to the core (needs the core's trace signal)" ON)
option(OVERISC_KONATA_REGRESSION "Write Kanata logs in the riscv-tests regression" ON)

find_package(verilator REQUIRED HINTS $ENV{VERILATOR_ROOT})
add_subdirectory(${OVERISC_ROOT}/third_party/Catch2 catch2 EXCLUDE_FROM_ALL)
include(CTest)
include(Catch)

set(COMMON_F ${OVERISC_ROOT}/rtl/common/common.f)
set(MEM_F ${CMAKE_CURRENT_SOURCE_DIR}/mem/mem.f)
set(VERILATOR_WARN -Wall)

# ---- C++ models (memory, AXI slave, ELF loader, DPI) ----
add_library(overisc_sim STATIC
  mem/sim_memory.cc
  mem/axi_slave.cc
  mem/elf_loader.cc
  mem/sim_dpi.cc
  trace/konata_tracer.cc
  trace/konata_label.cc
  trace/konata_dpi.cc
)
target_include_directories(overisc_sim PUBLIC
  mem
  trace
  harness
  ${OVERISC_ROOT}/third_party/ELFIO
  ${VERILATOR_ROOT}/include
  ${VERILATOR_ROOT}/include/vltstd
)
target_compile_options(overisc_sim PRIVATE -Wall -Wextra)

# Spike's disassembler for Kanata labels (optional)
find_library(SPIKE_DISASM disasm PATHS ${SPIKE_HOME}/lib NO_DEFAULT_PATH)
find_library(SPIKE_RISCV riscv PATHS ${SPIKE_HOME}/lib NO_DEFAULT_PATH)
if(SPIKE_DISASM AND SPIKE_RISCV)
  target_include_directories(overisc_sim PRIVATE ${SPIKE_HOME}/include)
  target_compile_definitions(overisc_sim PUBLIC OVERISC_HAVE_SPIKE_DISASM)
  target_link_libraries(overisc_sim PUBLIC ${SPIKE_DISASM} ${SPIKE_RISCV})
else()
  message(STATUS "Spike not found in ${SPIKE_HOME}; Kanata labels show raw encodings")
endif()

# ---- pure C++ unit tests ----
add_executable(test_unit
  tests/axi_mem/test_axi_slave.cc
  tests/host/test_elf_loader.cc
  tests/konata/test_konata.cc
)
target_link_libraries(test_unit PRIVATE overisc_sim Catch2::Catch2WithMain)
target_include_directories(test_unit PRIVATE tests/konata)
target_compile_definitions(test_unit PRIVATE
  RVTESTS_DIR="${RVTESTS_DIR}" RISCV_PREFIX="${RISCV_PREFIX}")
target_compile_options(test_unit PRIVATE -Wall -Wextra)
catch_discover_tests(test_unit PROPERTIES LABELS "unit" TIMEOUT 60)

# ---- Verilated tests ----
# The memory smoke test generates its own clock (timed model).
add_executable(test_axi_mem_glue tests/axi_mem/test_axi_mem_glue.cc)
target_link_libraries(test_axi_mem_glue PRIVATE overisc_sim Catch2::Catch2WithMain)
verilate(test_axi_mem_glue
  PREFIX Vtb_axi_mem TOP_MODULE tb_axi_mem
  SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/tests/axi_mem/tb_axi_mem.sv
  VERILATOR_ARGS ${VERILATOR_WARN} --timing -F ${COMMON_F} -F ${MEM_F})
catch_discover_tests(test_axi_mem_glue PROPERTIES LABELS "verilator")

# The host module alone, clocked from C++. The second model checks the reset-vector check.
add_executable(test_host tests/host/test_host.cc)
target_link_libraries(test_host PRIVATE overisc_sim Catch2::Catch2WithMain)
target_compile_definitions(test_host PRIVATE RVTESTS_DIR="${RVTESTS_DIR}")
verilate(test_host
  PREFIX Vovl_host TOP_MODULE overisc_sim_host
  SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/mem/overisc_sim_host.sv
  VERILATOR_ARGS ${VERILATOR_WARN})
verilate(test_host
  PREFIX Vovl_host_rv1000 TOP_MODULE overisc_sim_host
  SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/mem/overisc_sim_host.sv
  VERILATOR_ARGS ${VERILATOR_WARN} -GRESET_VECTOR=64'h1000)
catch_discover_tests(test_host PROPERTIES LABELS "verilator")

# Kanata logger driven straight from C++ (must match the direct writer).
set(TRACE_PKG ${OVERISC_ROOT}/rtl/core/overisc_trace_pkg.sv)
add_executable(test_konata_glue tests/konata/test_konata_glue.cc)
target_link_libraries(test_konata_glue PRIVATE overisc_sim Catch2::Catch2WithMain)
target_include_directories(test_konata_glue PRIVATE tests/konata)
verilate(test_konata_glue
  PREFIX Vtb_konata TOP_MODULE tb_konata
  SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/tests/konata/tb_konata.sv
  VERILATOR_ARGS ${VERILATOR_WARN}
    ${OVERISC_ROOT}/rtl/core/overisc_cfg_pkg.sv ${OVERISC_ROOT}/rtl/core/overisc_commit_pkg.sv
    ${TRACE_PKG} ${CMAKE_CURRENT_SOURCE_DIR}/trace/overisc_sim_konata.sv)
catch_discover_tests(test_konata_glue PROPERTIES LABELS "verilator")

# Pipeline buffers: overisc_reg_slice, overisc_skid_buffer (rtl/common), overisc_fetch_buffer
# (rtl/core; two depths).
set(PIPE_IF ${OVERISC_ROOT}/rtl/core/overisc_pipe_if.sv)
set(PIPE_BUF_SRCS
  ${OVERISC_ROOT}/rtl/core/overisc_cfg_pkg.sv ${OVERISC_ROOT}/rtl/core/overisc_commit_pkg.sv
  ${OVERISC_ROOT}/rtl/core/overisc_trace_pkg.sv ${OVERISC_ROOT}/rtl/core/overisc_pipe_pkg.sv
  ${PIPE_IF} -F ${COMMON_F} ${OVERISC_ROOT}/rtl/core/overisc_fetch_buffer.sv)
add_executable(test_pipe_buf tests/pipe/test_pipe_buf.cc)
target_link_libraries(test_pipe_buf PRIVATE overisc_sim Catch2::Catch2WithMain)
foreach(variant IN ITEMS "reg_slice;0;2" "skid_buffer;1;2" "fetch_buffer;2;2" "fetch_buffer16;2;4")
  list(GET variant 0 name)
  list(GET variant 1 kind)
  list(GET variant 2 depth_w)
  verilate(test_pipe_buf
    PREFIX Vtb_${name} TOP_MODULE tb_pipe_buf
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/tests/pipe/tb_pipe_buf.sv
    VERILATOR_ARGS ${VERILATOR_WARN} ${PIPE_BUF_SRCS} -GKIND=${kind} -GDEPTH_W=${depth_w})
endforeach()
catch_discover_tests(test_pipe_buf PROPERTIES LABELS "verilator")

# ---- core (B4) ----
# One CTest per riscv-test x seed x memory profile, labeled "riscv-tests".
set(REGRESSION_SEEDS "1" CACHE STRING "seeds for the riscv-tests regression")
set(REGRESSION_PROFILES "fast;small;large;random" CACHE STRING
    "memory timing profiles for the riscv-tests regression")
set(CORE_RESET_VECTOR "64'h8000_0000")
if(OVERISC_KONATA)
  set(KONATA_ARGS -DOVERISC_KONATA)
else()
  set(KONATA_ARGS)
endif()

# The core simulator in two builds that differ only in the FST waveform:
#   test_core       structs traced member by member (--trace-structs); the regression and
#                   `make run` use it, for riscv-tests and short instruction sequences
#   test_core_flat  structs traced as one bit vector; not built by default, for long runs
#                   (`make run TRACE_STRUCTS=0`, or cmake --build ... --target test_core_flat)
# Nothing is traced unless a run asks for an FST file, so the struct tracing costs nothing
# otherwise.
function(add_core_sim target)
  cmake_parse_arguments(ARG "TRACE_STRUCTS;EXCLUDE_FROM_ALL" "" "" ${ARGN})
  if(ARG_EXCLUDE_FROM_ALL)
    add_executable(${target} EXCLUDE_FROM_ALL tests/core/test_core.cc)
  else()
    add_executable(${target} tests/core/test_core.cc)
  endif()
  target_link_libraries(${target} PRIVATE overisc_sim Catch2::Catch2)
  if(ARG_TRACE_STRUCTS)
    set(structs TRACE_STRUCTS)
  else()
    set(structs)
  endif()
  verilate(${target}
    PREFIX Vtb_core TOP_MODULE tb_core TRACE_FST ${structs}
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/tb/tb_core.sv
    VERILATOR_ARGS ${VERILATOR_WARN} -F ${CMAKE_CURRENT_SOURCE_DIR}/sim.f
      -GRESET_VECTOR=${CORE_RESET_VECTOR}
      --x-assign unique --x-initial unique
      ${KONATA_ARGS})
endfunction()

add_core_sim(test_core TRACE_STRUCTS)
add_core_sim(test_core_flat EXCLUDE_FROM_ALL)

set(M0_LIST ${RVTESTS_DIR}/m0.list)
if(EXISTS ${M0_LIST})
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${M0_LIST})
  file(STRINGS ${M0_LIST} M0_TESTS)
  foreach(profile IN LISTS REGRESSION_PROFILES)
    foreach(seed IN LISTS REGRESSION_SEEDS)
      foreach(t IN LISTS M0_TESTS)
        # Kanata log per test, overwritten by the next run of the same test
        if(OVERISC_KONATA AND OVERISC_KONATA_REGRESSION)
          set(konata --konata ${CMAKE_BINARY_DIR}/konata/${t}_${profile}_s${seed}.log)
        else()
          set(konata)
        endif()
        add_test(NAME "riscv-tests/${profile}/${seed}/${t}"
          COMMAND test_core --elf ${RVTESTS_DIR}/${t} --seed ${seed} --profile ${profile}
            ${konata})
        set_tests_properties("riscv-tests/${profile}/${seed}/${t}" PROPERTIES
          LABELS "riscv-tests;core")
      endforeach()
    endforeach()
  endforeach()
else()
  message(WARNING "${M0_LIST} not found; run `make riscv-tests` and reconfigure")
endif()
