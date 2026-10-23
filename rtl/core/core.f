// SPDX-License-Identifier: CERN-OHL-P-2.0
// コア本体（合成可能なソース、+incdir+、合成でも必要な define のみ）
// パスはこのファイルからの相対パス（Verilator の -F で読む）
// SystemVerilog のパッケージを先頭に置く
-F ../common/common.f
overisc_cfg_pkg.sv
overisc_mem_pkg.sv
overisc_commit_pkg.sv
overisc_core_top.sv
