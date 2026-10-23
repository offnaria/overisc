// SPDX-License-Identifier: Apache-2.0
// シミュレーションで共通に使う部分（SoC/コアとシミュレーションモデル）
// テストベンチの最上位ファイル（sim/tb/*.sv など）は含めず、sim/CMakeLists.txt で試験ごとに指定する
// パスはこのファイルからの相対パス（Verilator の -F で読む）
-F ../rtl/soc/soc.f
-F mem/mem.f
