// SPDX-License-Identifier: CERN-OHL-P-2.0
// コア本体（合成可能なソース、+incdir+、合成でも必要な define のみ）
// パスはこのファイルからの相対パス（Verilator の -F で読む）
// SystemVerilog のパッケージを先頭に置く
-F ../common/common.f

// package
overisc_cfg_pkg.sv
overisc_mem_pkg.sv
overisc_commit_pkg.sv
overisc_pipe_pkg.sv
overisc_trace_pkg.sv

// interface
overisc_pipe_if.sv

// module
overisc_pcgen.sv
overisc_core_top.sv
