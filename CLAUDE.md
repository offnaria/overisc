# CLAUDE.md

このリポジトリは、趣味で開発する **Linux 起動可能な RV64 RISC-V コア**（SystemVerilog、FPGA 実装）です。
詳細な設計は `docs/design.md`、判断の経緯は `docs/decisions.md` にあります。設計に関わる変更の前に必ず両方を読んでください。

## プロジェクト概要

- 当面のゴール: **最新の Linux の起動**（OpenSBI → Linux → BusyBox シェル）
- ISA: **RV64IMA_Zicsr_Zifencei_Zicntr** + M/S/U モード + Sv39（C/F/D は後で追加）
- マイクロアーキテクチャ: インオーダー・スカラー、7 段（`F0 F1 F2 [FetchBuffer] D X M W`）
- 言語: SystemVerilog。シミュレーションは Verilator、リファレンスモデルは Spike
- 周波数目標（xc7a100t-1、コア単体 OOC、配置配線後）: 必達 100MHz / 目標 150MHz / 挑戦 200MHz

## 現在のマイルストーン: M0

**内容**: 協調シミュレーション基盤 + ユーザーレベル ISA（+ riscv-tests の `p` 環境が使う M-mode 最小サブセット）

**完了条件**
1. riscv-tests の `rv64ui-p-*`, `rv64um-p-*`, `rv64ua-p-*` がすべて PASS（`rv64ui-p-ma_data` は除く。D-31）
2. 全テストで Spike との命令単位の比較に不一致がない
3. CI（Verilator ビルド + テスト + Spike 比較）が通る
4. コア単体の OOC 合成（xc7a100t-1）の Fmax / LUT / FF を記録している

**M0 でやること**（詳細は `docs/design.md` §10）
- ディレクトリ構成と `.f` ファイルリストの階層
- コアと単純メモリ間の ready/valid インタフェース（SV パッケージと構造体）
- 7 段パイプラインの骨格（分岐予測なし、常に not-taken）
- スコアボード + フォワーディング、ロードの結果は 2 サイクル後に使用可能
- 乗算器（3〜4 段、DSP 向け）、除算器（反復）
- アトミック: LSU で read-modify-write、LR/SC 予約レジスタ 1 本
- M-mode 最小 CSR（mstatus, mtvec, mepc, mcause, mtval, mhartid, misa, mscratch）、MRET, ECALL
- 未実装 CSR へのアクセスは **必ず不正命令例外**
- C++ の単純メモリモデル（DPI）。待ち時間・ready/valid の遅延をランダム化（シードで再現可能）
- `tohost` 監視による終了判定
- 確定命令の記録（commit record）と Spike ライブラリとの比較
- コア単体の OOC 合成用 Tcl スクリプト

## リポジトリ構成（予定）

```
rtl/core/          コア本体（ボード非依存）
rtl/soc/           SoC（CLINT, UART, インターコネクト, SoC 情報レジスタ, 後に L2/PLIC）
rtl/tech/generic/  テクノロジーラッパーの動作記述版（RAM など。シミュレーションと推論合成用）
rtl/tech/xilinx/   同名モジュールの XPM 版（FPGA 実装時に差し替え）
sim/               Verilator テストベンチ、C++ メモリモデル、Spike 協調シミュレーション
sw/                テストプログラム、ブート ROM、デバイスツリー、ビルドスクリプト
syn/               OOC 合成スクリプト
boards/<board>/    ボード用トップラッパー、制約、IP（FPGA 実装段階で追加）
Top/<board>/       Hog のリストファイル（FPGA 実装段階で導入）
docs/              設計書と判断記録
third_party/       サブモジュール（riscv-tests, riscv-isa-sim = Spike）。D-32
```

## ビルド

```
git submodule update --init --recursive
make toolchain     # kernel.org crosstool の GCC を .tools/ に取得（D-33）
make spike         # Spike を build/spike/install にビルド
make riscv-tests   # M0 用テストを build/riscv-tests にビルド（実行対象は m0.list）
make lint          # コアの Verilator lint（-Wall、警告ゼロ）
```

## ファイルリストの規則

- **正は Verilator の `.f`**。階層: `rtl/core/core.f` → `rtl/soc/soc.f`（`-F` で core を含む）→ `sim/sim.f`（tb, DPI, Verilator オプション）
- `core.f` と `soc.f` には **合成可能なソースと `+incdir+`、合成でも必要な define だけ** を書く
- `.f` の中のパスはその `.f` ファイルからの相対パスで書き、入れ子は `-F` で読む
- SystemVerilog のパッケージはファイルリストの先頭に置く
- シミュレーション専用の記述は `` `ifdef VERILATOR `` で囲む
- 将来 Hog のリストファイルは `core.f`/`soc.f` から生成する（`rtl/tech/generic` → `rtl/tech/xilinx` の置換だけが意図的な差分）

## コーディング規約

- `always_ff` / `always_comb` を使う。ラッチを作らない
- Verilator の lint を警告ゼロに保つ
- **コアの RTL から XPM や Xilinx プリミティブを直接インスタンス化しない**（`rtl/tech/` のラッパー経由）
- **リセットは valid ビットと制御状態だけ**。データパスはリセットしない
- ready/valid の規則: valid は ready に依存させない。一度立てた valid は受理されるまで下げず内容も変えない
- valid の次状態は全段で同じ書き方にする: `valid_next = kill ? 0 : (stall ? valid : valid_in)`（**フラッシュはストールより優先**）
- 幅や数はパラメータ化する（バス幅 `DATA_W`、Fetch Buffer の出口幅、レジスタファイルのポート数など）
- 新しいファイルの先頭に `SPDX-License-Identifier` を書く。rtl/・syn/・boards/・Top/・docs/ は `CERN-OHL-P-2.0`、sim/・sw/・ビルドスクリプトは `Apache-2.0`（D-35）

## 壊してはいけない設計上の不変条件

1. **PC の bit1 を削らない**。命令に `is_rvc` を持たせパイプライン全体に流す（現状は常に 0）。次 PC とリンクアドレスは `is_rvc` を見て +2/+4 を選ぶ
2. **Fetch Buffer はハーフワード単位の FIFO**。整列は出口で行う。例外情報はハーフワードごと
3. **コアのメモリインタフェースは ready/valid の要求/応答**で、任意の待ち時間に耐えること（M4 でキャッシュを差し込む前提）
4. **リダイレクトは一本のバス**（`{valid, target_pc, src_stage, ...}`）で、最も古い段の要求を優先
5. **分岐ミスのリダイレクトは 1 段レジスタを通す**
6. **MMIO アクセスは非投機**（その命令が最も古くなってから出す）
7. **CSR 書き込みは確定後に全フラッシュして再フェッチ**（最適化は性能測定後、段階的に）
8. スコアボードの印は、フラッシュされた命令の分を必ず解除する
9. 協調シミュレーションの比較条件を、テストを通すために緩めない

## 検証の進め方

- どの変更でも riscv-tests と Spike 比較を通すこと。マイルストーンを達成したらそのテストを CI に追加する
- 不一致が出たら、確定命令の記録の直前部分と波形で原因を特定する。Spike 側を疑う前に DUT を疑う
- 時間や MMIO に依存する値（mcycle, time, instret, CLINT/UART の読み出し）は DUT の値を Spike に注入する
- 割り込みは DUT が受け付けたタイミングで Spike の mip を設定して発生させる

## 作業上のお願い

- `docs/design.md` に書かれた判断と異なる実装をしたくなったら、**実装前に理由を添えて確認**すること
- 新たに設計判断をしたら `docs/decisions.md` に追記する（形式は既存エントリに合わせる）
- 「要確認」と書かれた項目は、実際のツールやドキュメントで確認してから使う
