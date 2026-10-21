# SPDX-License-Identifier: Apache-2.0
# Overisc のトップレベル Makefile
#
#   make toolchain    kernel.org crosstool の GCC を .tools/ に取得（D-33）
#   make spike        Spike をビルドして build/spike/install に入れる
#   make riscv-tests  M0 用の riscv-tests を build/riscv-tests にビルド
#   make lint         コアの Verilator lint（警告ゼロが規則）
#   make format       SystemVerilog を Verible で整形（既定の設定）
#   make format-check 整形済みかどうかだけ確認（CI 用）
#   make clean        build/ を削除（.tools/ は残す）

ROOT  := $(abspath .)
BUILD := $(ROOT)/build
TOOLS := $(ROOT)/.tools

# ---- ツールチェーン ----
GCC_VER       := $(shell sed -n 's/^GCC_VER=//p' $(ROOT)/sw/scripts/get-toolchain.sh)
TOOLCHAIN_BIN := $(TOOLS)/gcc-$(GCC_VER)-nolibc/riscv64-linux/bin
RISCV_PREFIX  := $(TOOLCHAIN_BIN)/riscv64-linux-

.PHONY: toolchain
toolchain: $(RISCV_PREFIX)gcc

$(RISCV_PREFIX)gcc:
	$(ROOT)/sw/scripts/get-toolchain.sh $(TOOLS)

# ---- Spike ----
SPIKE_SRC   := $(ROOT)/third_party/riscv-isa-sim
SPIKE_BUILD := $(BUILD)/spike
SPIKE_HOME  := $(SPIKE_BUILD)/install

# 毎回 Spike の make を呼ぶ（差分ビルドなので速い）。サブモジュール更新時もそのまま追従する
.PHONY: spike
spike: $(SPIKE_BUILD)/Makefile
	$(MAKE) -C $(SPIKE_BUILD)
	$(MAKE) -C $(SPIKE_BUILD) install

$(SPIKE_BUILD)/Makefile:
	mkdir -p $(SPIKE_BUILD)
	cd $(SPIKE_BUILD) && $(SPIKE_SRC)/configure --prefix=$(SPIKE_HOME)

# ---- riscv-tests ----
RVTESTS_SRC   := $(ROOT)/third_party/riscv-tests
RVTESTS_BUILD := $(BUILD)/riscv-tests

# isa/Makefile は PATH 上に $(RISCV_PREFIX)gcc があるかを which で調べるので、絶対パスでも PATH に通す
.PHONY: riscv-tests
riscv-tests: $(RISCV_PREFIX)gcc
	mkdir -p $(RVTESTS_BUILD)
	PATH=$(TOOLCHAIN_BIN):$$PATH $(MAKE) -C $(RVTESTS_BUILD) \
	    -f $(RVTESTS_SRC)/isa/Makefile -f $(ROOT)/sw/riscv-tests/tests.mk \
	    src_dir=$(RVTESTS_SRC)/isa XLEN=64 RISCV_PREFIX=riscv64-linux- m0

# ---- lint ----
CORE_TOP ?= overisc_core
VERILATOR ?= verilator

# 最上位モジュール（ファイル名 = モジュール名）がまだない間はパッケージだけを lint する
# （参照元がないので未使用パラメータの警告は止める）
LINT_TOP := $(if $(wildcard $(ROOT)/rtl/core/$(CORE_TOP).sv),--top-module $(CORE_TOP),-Wno-UNUSEDPARAM)

.PHONY: lint
lint:
	$(VERILATOR) --lint-only -Wall -F $(ROOT)/rtl/core/core.f $(LINT_TOP)

# ---- format ----
VERIBLE_FORMAT ?= verible-verilog-format
SV_SRCS = $(shell find $(ROOT)/rtl $(ROOT)/sim -name '*.sv' -o -name '*.svh' 2>/dev/null)

.PHONY: format format-check
format:
	$(if $(SV_SRCS),$(VERIBLE_FORMAT) --inplace $(SV_SRCS))

format-check:
	$(if $(SV_SRCS),$(VERIBLE_FORMAT) --inplace --verify $(SV_SRCS))

.PHONY: clean
clean:
	rm -rf $(BUILD)
