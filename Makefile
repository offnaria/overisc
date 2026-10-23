# SPDX-License-Identifier: Apache-2.0
# Overisc のトップレベル Makefile
#
#   make toolchain    kernel.org crosstool の GCC を .tools/ に取得（D-33）
#   make spike        Spike をビルドして build/spike/install に入れる
#   make riscv-tests  M0 用の riscv-tests を build/riscv-tests にビルド
#   make lint         コアの Verilator lint（警告ゼロが規則）
#   make format       SystemVerilog を Verible で整形（既定の設定）
#   make test-axi-mem シミュレーション用メモリモデルの単体試験（B9）
#   make test-host    ELF 読み込みと tohost 監視の単体試験（B3）
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
CORE_TOP ?= overisc_core_top
VERILATOR ?= verilator

# 最上位モジュール（ファイル名 = モジュール名）がまだない間はパッケージだけを lint する
# （参照元がないので未使用パラメータの警告は止める）
LINT_TOP := $(if $(wildcard $(ROOT)/rtl/core/$(CORE_TOP).sv),--top-module $(CORE_TOP),-Wno-UNUSEDPARAM)

.PHONY: lint
lint:
	$(VERILATOR) --lint-only -Wall -F $(ROOT)/rtl/core/core.f $(LINT_TOP)

# ---- シミュレーション用メモリモデルの単体試験（B9） ----
CXX       ?= g++
CXXFLAGS  ?= -std=c++17 -O2 -Wall -Wextra
SIM_MEM   := $(ROOT)/sim/mem
AXI_TEST  := $(BUILD)/tests/axi_mem
AXI_SEEDS ?= 1 2 3 4 5
AXI_PROFILES := fast small large random

$(AXI_TEST)/test_axi_slave: $(ROOT)/sim/tests/axi_mem/test_axi_slave.cc $(wildcard $(SIM_MEM)/*.cc $(SIM_MEM)/*.h)
	mkdir -p $(AXI_TEST)
	$(CXX) $(CXXFLAGS) -I$(SIM_MEM) $< $(SIM_MEM)/axi_slave.cc $(SIM_MEM)/sim_memory.cc -o $@

$(AXI_TEST)/Vtb_axi_mem: $(ROOT)/sim/tests/axi_mem/tb_axi_mem.sv $(wildcard $(SIM_MEM)/*) $(ROOT)/rtl/common/overisc_axi_pkg.sv
	$(VERILATOR) --binary --timing -j 0 -Wall --top-module tb_axi_mem \
	    -F $(ROOT)/rtl/common/common.f -F $(SIM_MEM)/mem.f $< \
	    -CFLAGS "-std=c++17 -I$(SIM_MEM)" --Mdir $(AXI_TEST)/obj -o $@ > $(AXI_TEST)/verilate.log

.PHONY: test-axi-mem
test-axi-mem: $(AXI_TEST)/test_axi_slave $(AXI_TEST)/Vtb_axi_mem
	@set -e; for p in $(AXI_PROFILES); do for s in $(AXI_SEEDS); do \
	    $(AXI_TEST)/test_axi_slave $$s $$p 100000; done; done
	@set -e; for p in $(AXI_PROFILES); do for s in $(AXI_SEEDS); do \
	    log=$(AXI_TEST)/tb_$${p}_$${s}.log; \
	    $(AXI_TEST)/Vtb_axi_mem +seed=$$s +mem_profile=$$p > $$log 2>&1 || true; \
	    grep -E '^(PASS|FAIL)|ERROR|violation' $$log; grep -q '^PASS' $$log; done; done

# ---- ELF 読み込みと tohost 監視の単体試験（B3） ----
HOST_TEST := $(BUILD)/tests/host

$(HOST_TEST)/test_elf_loader: $(ROOT)/sim/tests/host/test_elf_loader.cc $(wildcard $(SIM_MEM)/*.cc $(SIM_MEM)/*.h)
	mkdir -p $(HOST_TEST)
	$(CXX) $(CXXFLAGS) -I$(SIM_MEM) $< $(SIM_MEM)/elf_loader.cc $(SIM_MEM)/sim_memory.cc -o $@

$(HOST_TEST)/Vtb_host: $(ROOT)/sim/tests/host/tb_host.sv $(wildcard $(SIM_MEM)/*) $(ROOT)/rtl/common/overisc_axi_pkg.sv
	$(VERILATOR) --binary --timing -j 0 -Wall --top-module tb_host \
	    -F $(ROOT)/rtl/common/common.f -F $(SIM_MEM)/mem.f $< \
	    -CFLAGS "-std=c++17 -I$(SIM_MEM)" --Mdir $(HOST_TEST)/obj -o $@ > $(HOST_TEST)/verilate.log

# 期待する結果の行の先頭とプラス引数の組
HOST_CASES := PASS:+fake_tohost=1 FAIL:+fake_tohost=7 TIMEOUT:+max_cycles=500 \
              ERROR:+elf=/nonexistent

.PHONY: test-host
test-host: riscv-tests $(HOST_TEST)/test_elf_loader $(HOST_TEST)/Vtb_host
	@set -e; n=0; for t in $$(cat $(RVTESTS_BUILD)/m0.list) rv64ui-p-ma_data; do \
	    elf=$(RVTESTS_BUILD)/$$t; \
	    $(RISCV_PREFIX)objcopy -O binary $$elf $(HOST_TEST)/$$t.bin; \
	    th=$$($(RISCV_PREFIX)nm $$elf | awk '$$3 == "tohost" { print $$1 }'); \
	    $(HOST_TEST)/test_elf_loader $$elf $(HOST_TEST)/$$t.bin $$th; n=$$((n+1)); done; \
	    echo "PASS test_elf_loader ($$n ELF files)"
	@set -e; elf=$(RVTESTS_BUILD)/rv64ui-p-simple; for c in $(HOST_CASES); do \
	    want=$${c%%:*}; arg=$${c#*:}; log=$(HOST_TEST)/tb_$$want.log; \
	    $(HOST_TEST)/Vtb_host $$arg +elf=$$elf > $$log 2>&1 || true; \
	    line=$$(grep -E '^(PASS|FAIL|TIMEOUT|ERROR)' $$log | head -1); \
	    case "$$line" in "$$want"*) echo "ok   [$$want] $$line";; \
	    *) echo "FAIL expected $$want, got: $$line"; exit 1;; esac; done

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
