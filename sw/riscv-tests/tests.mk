# riscv-tests の isa/Makefile と一緒に読み込み、必要なテストだけをビルドする。
#   make -C <出力先> -f <riscv-tests>/isa/Makefile -f tests.mk src_dir=<riscv-tests>/isa <ターゲット>
# ターゲット:
#   m0      M0 で使う p 環境のテスト（ELF と .dump）と、実行対象の一覧 m0.list

# M0 の完了条件から除くテスト（D-31）
m0_exclude := rv64ui-p-ma_data

m0_tests := $(rv64ui_p_tests) $(rv64um_p_tests) $(rv64ua_p_tests)

m0: $(addsuffix .dump, $(m0_tests)) m0.list

m0.list: FORCE
	@printf '%s\n' $(filter-out $(m0_exclude), $(m0_tests)) > $@.tmp
	@cmp -s $@.tmp $@ && rm $@.tmp || mv $@.tmp $@

.PHONY: m0 FORCE
