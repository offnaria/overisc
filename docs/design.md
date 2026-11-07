# 設計書

RV64 インオーダー RISC-V コア（Linux 起動可能、FPGA 実装）の設計方針。
判断の理由と検討した代替案は `decisions.md`（D-xx 番号で参照）を参照。

「要確認」と書いた項目は、記憶や一般的な情報に基づくもので、実際のツール/仕様で確認してから使うこと。

---

## 1. 前提と目標

| 項目 | 内容 |
|---|---|
| 目的 | 趣味。Linux が起動する RISC-V コアを自作する |
| 当面のゴール | 最新の安定版 Linux の起動（OpenSBI → Linux → initramfs の BusyBox） |
| ISA | RV64IMA_Zicsr_Zifencei_Zicntr + M/S/U + Sv39 |
| 後で追加 | C 拡張、F/D、Sstc、Zicbom、B 拡張など |
| RTL | SystemVerilog |
| シミュレーション | Verilator + Spike（ライブラリとしてリンク）による協調シミュレーション |
| 実装先 | AMD 7 シリーズ FPGA（ボードは riscv-tests 通過後に決定） |
| 周波数目標 | xc7a100t-1、コア単体（I$/D$/TLB 含む）OOC 合成、配置配線後: 必達 100 / 目標 150 / 挑戦 200 MHz |
| 将来 | マルチコア（段階的）、インオーダー 2-way |

FPGA の BRAM は同期読み出し（結果が 1 サイクル後）であることが、ステージ構成を決める最大の制約。

---

## 2. マイルストーン

| # | 内容 | 完了条件 |
|---|---|---|
| M0 | 協調シミュレーション基盤 + ユーザーレベル ISA | `rv64ui/um/ua-p-*` PASS（`ma_data` を除く）、Spike と不一致なし、CI 通過、OOC 合成結果を記録 |
| M1 | 特権アーキテクチャ + Sv39 | `rv64mi-p-*`, `rv64si-p-*`, `rv64ui-v-*` PASS |
| M2 | OpenSBI の起動 | OpenSBI のバナーと同梱の試験用ペイロードのメッセージが出る。Spike と不一致なし |
| M3 | Linux 起動（シミュレーション、キャッシュなし） | initramfs の BusyBox でシェル（`/ #`）が出る |
| M4 | キャッシュとバス | M3 と同じ Linux 起動が I$/D$/ストアバッファ/二つのポート込みで通る |
| M5 | FPGA 上で Linux 起動 | 実機でシェルが使える |

- M3 までキャッシュなしで進め、M3 の Linux 起動を M4 の退行テストにする（D-03）
- そのため **M0 からコアのメモリインタフェースは ready/valid で、可変の待ち時間に対応** させる
- ソフトウェア一式は QEMU → Spike → RTL の順に同じイメージで確認する

M5 以降の順序（目安）:
C 拡張 → 分岐予測（BTB/bimodal/RAS）と CSR フラッシュの最適化 → 共有 L2 によるマルチコア（一貫性段階 2）+ IO コヒーレンシ → ノンブロッキングキャッシュ → インオーダー 2-way → MESI（一貫性段階 3）

---

## 3. パイプライン

```
F0 → F1 → F2 ═[Fetch Buffer]═→ D → X → M → W
```

| 段 | 役割 |
|---|---|
| F0 | 次 PC 選択、I$ インデックス送出、iTLB 参照、BTB 参照 |
| F1 | I$ の tag/data が出る。iTLB の物理タグと比較、way 選択。BTB の結果 |
| F2 | 命令の切り出し、プリデコードによる早期リダイレクト（JAL、BTFN） |
| Fetch Buffer | 命令単位の FIFO。フロントエンドとバックエンドのストールを切り離す（D-41） |
| D | デコード、レジスタファイル読み出し、ハザード検出（スコアボード） |
| X | ALU、分岐確定（ミス時リダイレクト）、乗算の開始 |
| M | D$/メモリアクセス、ロード/ストア、アトミック |
| W | 書き戻し、例外・割り込みの確定、CSR 書き込み |

### 3.1 ハザードと実行ユニット
- スコアボード方式（各レジスタの値がいつ使えるかを表で管理）+ フォワーディング
- **ロードの結果は 2 サイクル後に使用可能**（200MHz 目標のため。D-21）
- 乗算器: DSP48 向けに 3〜4 段パイプライン。除算器: 反復（複数サイクル）
- CSR 読み出しは専用の 1 サイクルを使ってよい（CSR 命令は直列化するため）
- フォワーディング元は絞る（X の ALU → フォワード → ALU のループがクリティカルになりやすい）

### 3.2 分岐予測（段階的。D-08）
| 段階 | 仕組み | taken 分岐のペナルティ |
|---|---|---|
| ①（M0〜） | 常に not-taken、X で確定 | 4 サイクル + リダイレクトのレジスタ 1 |
| ② | F2 で JAL をプリデコード、条件分岐は BTFN | 予測が当たれば 2 サイクル |
| ③ | BTB（BRAM、ペナルティ 1）+ bimodal BHT + RAS | 予測が当たれば 1 サイクル |

- BTB: 命令（4B）単位でインデックス、32〜64 エントリ、ダイレクトマップか 2-way
  - エントリ: `valid | 部分tag(10-12b) | type(3b: none/cond/jal/jalr/call/ret) | is_rvc | target`
  - 8B フェッチにするときは、フェッチブロック単位のインデックスと `pos(2b)` を再検討する（D-41）
- BHT: 2bit カウンタ 512〜1024 エントリ（bimodal）、X で更新。gshare は後回し
- RAS: 8 エントリ。call = rd が x1/x5 の JAL/JALR、ret = rs1 が x1/x5 で rd=x0 の JALR。リダイレクト時はフェッチパケットに保存したポインタに戻す
- ゼロバブル化は性能測定後に小さな L0 BTB で

### 3.3 フラッシュ（D-13）
- 各パイプラインレジスタの **valid ビットだけ** をクリア
- **リダイレクトバスを一本に統一**: `redirect = {valid, target_pc, src_stage, ras_ptr, (ghist)}`
  - 各段の `kill = redirect.valid && (src_stage が自分より古い)`
  - 同時に複数あれば最も古い段を優先（中央の優先度回路）
- `valid_next = kill ? 0 : (stall ? valid : valid_in)`（フラッシュはストールより優先）
- 分岐ミスのリダイレクトは 1 段レジスタを通す（タイミングのため）

| 発生源 | 段 | 消す範囲 |
|---|---|---|
| BTB の taken 予測 | F1 | F0 |
| F2 の早期リダイレクト | F2 | F0, F1 |
| 分岐ミス | X | F0〜F2, Fetch Buffer, D |
| 例外・割り込み・xRET | W | 全段 |
| 直列化命令の確定後 | W | 全段（次 PC から再フェッチ） |

取り消せないものの扱い:
- I$ のリフィル: バス要求は最後まで行う（キャッシュに書いてよい）が、パイプラインには渡さず捨てる
- MMIO アクセス: その命令が最も古くなってから出す（実質 W 相当）
- ストアバッファの中身: 確定済みなので消さない
- 除算器など長い演算: 中止するか、結果の書き戻しを捨てる
- スコアボードの印: フラッシュされた命令の分を解除する（忘れるとデッドロック）
- Fetch Buffer: ポインタを揃えて一括クリア

### 3.4 CSR と直列化命令（D-14）
- **最初は全 CSR 書き込みで、W で確定後に全フラッシュして再フェッチ**。CSR 読み出しも直列化してよい
- MRET/SRET/WFI/FENCE.I/SFENCE.VMA も同じ「確定後にフラッシュして再フェッチ」の経路
- 将来の最適化（性能カウンタで頻度を測ってから、段階的に）:

| 分類 | 例 | 対処 |
|---|---|---|
| A. データのみ | sscratch, sepc, scause, stval, stvec, カウンタ書き込み | 不要 |
| B. 割り込み許可 | sstatus.SIE, mstatus.MIE, sie, mie | 不要（W で書き、次サイクルの W での割り込み判定に反映） |
| C. デコード時の合法性・実行パラメータ | mstatus.FS/VS, TVM/TW/TSR, *counteren, *envcfg, frm | 後続を D で待たせる |
| D. データ側の変換・権限 | SUM, MXR, MPRV | 後続を D で待たせる |
| E. フェッチに影響 | satp, PMP, misa | フラッシュして再フェッチ |

  - sstatus/mstatus は同一 CSR 内に分類が混在するので、**実際に変化したビット** で判定する
  - 最初の最適化対象は sstatus.SIE（Linux の irqsave/irqrestore で頻繁）
  - 最適化のたびに Spike 比較を回す

---

## 4. フロントエンドと命令キャッシュ

### 4.1 I$（M4 から）
- 16KB、4-way、64B ライン、**VIPT**（1way = 4KB でインデックス+オフセットがページ内 12bit に収まり、エイリアスなし）
- データ配列は 64bit × 8 バンク（フェッチは 1〜2 バンク読み、128bit リフィルは 1 ビートで 2 バンク書き）
- **valid ビットはフリップフロップ**（`fence.i` で 1 サイクル一括無効化）
- フェッチ幅 4B/cycle（D-41）
- ミスはブロッキング。next-line プリフェッチ程度（FDIP はスカラーでは不採用。D-05）
- リフィルは WRAP バーストで critical-word-first
- 置換は PLRU か random

### 4.2 Fetch Buffer（D-41）
- 命令単位の FIFO。F2 から 1 命令ずつ push し、D へ 1 命令ずつ pop する
- 段数はパラメータ
- 例外情報（af/pf など）は命令ごと
- 一括クリアはポインタを揃える
- **8B フェッチの案（将来の候補）**: ハーフワード単位の FIFO に F2 から 4 ハーフワードをマスク付きで push し、出口で `is_rvc` を見て分割する（D-07）。C 拡張またはスーパースカラの段階で再検討する

### 4.3 プリデコード（D-09）
- スカラーの段階では I$ に保存しない（F2 で命令ビットから計算）
- 保存する場合（2-way 化やタイミング改善時）: ハーフワードあたり 4bit `rvc(1) | type(3)`、リフィル経路でビートごとに計算
- ラインの最終ハーフワードから始まる JALR は rs1 が判定できないので「種別不明の JALR」とし F2 で確定

### 4.4 iTLB / MMU
- iTLB 8 エントリ フルアソシアティブ（dTLB も同程度）。PTW は I/D で共有
- Sv39: 3 段、2MB/1GB の大きなページ対応（整列していない大きなページはページフォルト）
- 権限: R/W/X/U、SUM、MXR
- A/D は Svade（A=0、またはストアで D=0 ならページフォルト）
- ASID は 0bit でよい
- SFENCE.VMA は rs1/rs2 を無視して全 TLB 消去でよい
- PMA: アドレスごとに cacheable/MMIO を決め、存在しないアドレスはアクセスフォルト

### 4.5 L0（D-10）
- 他の改良をやり尽くしてから検討。作るなら展開済み命令を持つループバッファ型、VIVT、権限ビット付き、L1 から直接のバイパス必須

### 4.6 C 拡張への準備（D-06）
- PC の bit1 を残す、`is_rvc` をパイプライン全体に流す
- Fetch Buffer の単位と展開器の位置は、C 拡張を入れるときに再検討する（D-41）。4B 境界をまたぐ 32bit 命令が現れるので、ハーフワード単位の整列が必要になる
- C 拡張なしの間は IALIGN=32（分岐先が 4B 境界でなければ命令アドレスミスアライン例外）

---

## 5. メモリインタフェースとバス

### 5.1 コア内部インタフェース（D-15）
ready/valid（AXI と同じ規則: valid は ready に依存しない、受理まで保持）

```
REQ  (fetch/LSU -> cache slot): op, paddr, size, wstrb, wdata, id, src(I/D/PTW)
RESP (cache slot -> fetch/LSU): rdata, id, err(af/hwe)
```
- フェッチ/LSU とキャッシュの位置（M0〜M3 は AXI への変換器）の間の形式。データは 64bit（D-36）
- 命令側 1 本、データ側はキャッシュ側と MMIO 側の 2 本
- `op`: 読み出し、部分書き込み、後で cbo 系（AMO は L1 内で処理するので含めない。M0〜M3 のキャッシュなし構成では LSU が read-modify-write）
- `id`: 将来のノンブロッキングキャッシュ用に最初から持つ
- ライン単位の転送（WRAP バースト、critical-word-first）はキャッシュと AXI の間で扱う
- モジュールのポートは packed 構造体の req/resp（Interface は使わない。D-36）
- コアの最上位は M0 から AXI 系の 3 ポート（§5.2）。M0〜M3 はキャッシュの位置に REQ/RESP → AXI の変換器を置き、M4 でキャッシュに差し替える（D-34）
- タイミングが厳しい所にはスキッドバッファ

制御系:
- 全体に対する制御（`fence.i` による I$ 一括無効化、ストアバッファを空にする要求）: 専用信号の `req`/`done` 握手
- アドレスを伴う一貫性メッセージ（段階 2 以降の無効化）: ready/valid のチャネル（ACE の AC に相当）
- LSU は「ストアバッファが空で全応答受領済み」の状態信号を出す

### 5.2 外部バス（D-16, D-17）
- **コアのポートは合計 3 本**（M0 から。D-34）:
  - 命令側: ACE 128bit（段階 1 は実質 AXI4）。キャッシュ可能領域だけを読む（ブート ROM はキャッシュ可能領域に置く）
  - データ側のキャッシュ側: ACE 128bit（段階 1 は実質 AXI4）
  - データ側の MMIO 側: AXI4-Lite 64bit（コアからの MMIO アクセスは最大 8B）
- ACE の追加信号は段階 2 で足す。それまでは AXI4
- PTW と Zicbom はキャッシュ側
- データ側の二つのポートの間の順序はコアが保証:
  - MMIO アクセスは強い順序（それより前のメモリ操作がすべて完了してから出す）
  - `fence w,o` などはキャッシュ側の書き込みの応答（B）を待つ
- CLINT/PLIC は 64bit 側に直結（mtime の分割読みによるずれを防ぐ）
- 32bit の周辺回路は 64→32 変換の先に置く
- MMIO への AMO は PMA でアクセスフォルト

### 5.3 ACE の段階（D-18）
| 段階 | L1 D$ | L1 I$ | DMA ポート |
|---|---|---|---|
| 1（単一コア） | ReadNoSnoop / WriteNoSnoop（実質 AXI4、ACE 追加信号は定数） | ReadNoSnoop | なし |
| 2（ライトスルー + 共有 L2） | ReadShared、WriteUnique、無効化の問い合わせにデータなしで応答 | ReadClean、問い合わせに無効化のみ | ACE-Lite（ReadOnce, WriteUnique, WriteLineUnique） |
| 3（MESI） | + ReadUnique, CleanUnique, WriteBack, Evict、データ付き応答（CD） | 同上 | 同上 |

- **RACK/WACK を必ず実装**
- バリアトランザクションは使わない（AxBAR=0）
- L2 → DRAM は AXI4 128bit

### 5.4 一貫性の段階（D-19, D-20）
| 段階 | 構成 | DMA との一貫性 | FENCE.I |
|---|---|---|---|
| 1 | 単一コア、L1 D$ ライトスルー | Zicbom（ソフトウェア） | ストアバッファを空にして I$ 一括無効化 |
| 2 | 共有 L2（インクルーシブ、どの L1 がラインを持つか記録）、L1 ライトスルー | DMA ポートを L2 に接続（IO コヒーレンシ）、DT に `dma-coherent` | I$ も追跡対象にして無効化を受け付ける |
| 3 | L1 D$ ライトバック + MESI | L1 への問い合わせを流用 | I$ リフィルを一貫性のある読み出しにする |

- DMA デバイス自体は ACE を話す必要なし（L2 側で解決）。L2 は部分書き込みのバイトマスクマージが必要。DMA の L2 ミスはアロケートせず DRAM へ
- I$ に一貫性を持たせても、FENCE.I 命令（Fetch Buffer/パイプラインのフラッシュとストアバッファの排出）と Linux のリモート FENCE.I（IPI）は残る
- Zicbom は段階 2 以降も実装しておく

### 5.5 ストアバッファ（M4 から本格化。D-22）
- W で確定したストアを入れる。トラップ時も消さず書き出し続ける。物理アドレスで保持
- エントリ: `valid | paddr | data(64b) | byte mask | attr | state(待機/送出済み/応答待ち)`、4〜8 エントリ
- **下流の応答が返るまでエントリを残す**（FENCE で正しく待つため）
- ロードとの関係: 最初は「アドレスが一致したら待つ」、後で「完全に含まれていれば転送」
- アドレス比較のみ全エントリ並列（制御フィールドは FF、データは LUTRAM）。空/満杯はポインタ、データは番号指定読み出し、応答は ID で索引
- タイミング対策: ページ内オフセット 12bit で先に比較（待つ方式なら偽一致は性能のみの問題）、判定を 1 サイクル遅らせる、結合先は末尾エントリのみ
- 結合は RVWMO で合法。FENCE や `.rl` をまたいで結合しない
- FENCE/FENCE.I/AMO の前に全部書き出す。MMIO ストアはバッファが空になってから出し結合しない。cbo は該当ラインを先に書き出す

---

## 6. 特権アーキテクチャ（M0〜M2）

### 6.1 M0（riscv-tests `p` 環境に必要な最小）
- mstatus, mtvec（ダイレクト）, mepc, mcause, mtval, mhartid, misa, mscratch, MRET, ECALL
- 未実装 CSR へのアクセスは不正命令例外（テストの初期化コードが PMP/satp の書き込みを試み、例外なら読み飛ばすため）

### 6.2 M1
- モード: M/S/U
- mstatus/sstatus: MIE, SIE, MPIE, SPIE, MPP, SPP, MPRV, SUM, MXR, TVM, TW, TSR。FS=0 固定、UXL/SXL=64 固定
- 識別: misa（RV64 IMASU）、mvendorid, marchid, mimpid（git のハッシュでもよい）、mhartid、mconfigptr=0
- トラップ: mtvec/stvec（ダイレクト必須）、medeleg/mideleg、mie/mip/sie/sip、mepc/sepc、mcause/scause、mtval/stval、mscratch/sscratch
- カウンタ: mcounteren, scounteren, mcountinhibit, mcycle, minstret、cycle/time/instret（Zicntr）。mhpmcounter3-31/mhpmevent は 0 固定可
- menvcfg/senvcfg: 0 固定可
- PMP: 8 エントリ（TOR/NAPOT）
- 命令: MRET, SRET, WFI（nop 可）, SFENCE.VMA
- 割り込み: MSI/MTI/MEI, SSI/STI/SEI。優先順位と委譲に従う。Sstc がない間は OpenSBI が mip.STIP を書く
- 例外と tval: 命令アドレスミスアライン、命令アクセスフォルト、不正命令（tval に命令ビット推奨）、ブレークポイント、ロード/ストアのミスアラインとアクセスフォルト、U/S/M からの ECALL、命令/ロード/ストアのページフォルト
- ミスアラインアクセス: 例外にして OpenSBI にエミュレートさせる。アトミックのミスアラインは例外
- **time CSR はハードウェアで mtime を直接読む**（エミュレートは遅い）
- MPRV（MPP=S で Sv39 を通る組み合わせ）を試験しておく（OpenSBI が使う）

### 6.3 OpenSBI が起動時に行う検出（M2 のための注意）
- CSR へのアクセスで例外が起きるかどうかで特権仕様のバージョンや拡張を判定する → 未実装 CSR は正しく不正命令例外に
- mhpmcounter は書いて読み戻して実装数を数える → 0 固定なら未実装扱い
- medeleg/mideleg、mip.STIP の書き込み

---

## 7. SoC とプラットフォーム（M2〜）

### 7.1 メモリマップ（QEMU `virt` に準拠。D-23）
| 領域 | アドレス | サイズ | 備考 |
|---|---|---|---|
| ブート ROM | `0x0000_1000` | 4KB | リセットベクタ |
| CLINT | `0x0200_0000` | 64KB | mtime, mtimecmp, msip |
| PLIC | `0x0c00_0000` | | M5 まで省略可 |
| UART | `0x1000_0000` | 4KB | NS16550A 互換 |
| SoC 情報レジスタ | 未定 | | git ハッシュなど |
| DRAM | `0x8000_0000` | シミュレーション 128MB | サイズはパラメータ |

- **一つの定義ファイル（YAML など）から生成**: RTL の SV パッケージ、デバイスツリー、テストベンチのデコード、Spike の起動オプション

### 7.2 ブート ROM
- Spike と同じ動作: `a0 = mhartid`、`a1 = DTB のアドレス`（ROM 内）、`0x8000_0000` へジャンプ
- 同じ DTB を Spike にも `--dtb` で渡し、最初の命令から一致させる

### 7.3 CLINT と時間（協調シミュレーションの要点）
- mtime は DUT のサイクル数から進める（N サイクルごとに +1）。その周波数を DT の `timebase-frequency` に
- **Spike 側の時間は使わない**: CLINT と time CSR の読み出し値は DUT から注入、タイマー/ソフトウェア割り込みは DUT が受け付けたタイミングで Spike の mip を設定

### 7.4 UART
- シミュレーションモデル: THR 書き込みを標準出力へ、LSR は常に送信可能
- DT: `compatible = "ns16550a"`, `clock-frequency`, `reg-shift`, `reg-io-width`

### 7.5 デバイスツリー（M2/M3）
- cpus: `riscv,isa-base = "rv64i"`、`riscv,isa-extensions = "i","m","a","zicsr","zifencei","zicntr"`、`mmu-type = "riscv,sv39"`
- `timebase-frequency`、memory、CLINT（`riscv,clint0`）、UART、chosen（`bootargs`, `stdout-path`）
- M3 のコンソールは SBI コンソールでもよい（UART ドライバと PLIC が不要）

### 7.6 将来
- PLIC（M5 前）、Sstc（早めに入れる価値あり）、Zicbom、デバッグモジュール（RISC-V Debug 仕様、`riscv-dbg` の再利用を検討）

---

## 8. ソフトウェア

| 対象 | 設定の目安 |
|---|---|
| ツールチェーン | kernel.org crosstool の最新 GCC（`riscv64-linux-`、libc なし）で riscv-tests / ブート ROM / OpenSBI / Linux をビルド。`-march=rv64ima_zicsr_zifencei -mabi=lp64`。ユーザーランドは Buildroot が作るツールチェーン（D-33） |
| OpenSBI | 最新リリース。`PLATFORM=generic PLATFORM_RISCV_ISA=rv64ima_zicsr_zifencei PLATFORM_RISCV_ABI=lp64`、`FW_PAYLOAD`（M2 はペイロード未指定で同梱の試験用ペイロード、M3 は Linux Image）、`FW_TEXT_START=0x80000000` |
| Linux | 最新安定版。`CONFIG_RISCV_ISA_C=n`、`CONFIG_FPU=n`、`CONFIG_HVC_RISCV_SBI=y`（依存関係はバージョンにより要確認）、earlycon=sbi、それ以外は最小構成 |
| Buildroot | ISA 個別指定で C/F/D を外す、ABI lp64（ソフトウェア浮動小数点）、initramfs に BusyBox（設定項目名は要確認） |
| QEMU | `virt` マシン、CPU の C/F/D を無効化（他のデフォルト拡張も外す。オプション名は要確認） |
| Spike | `--isa=rv64ima_zicsr_zifencei_zicntr --priv=msu --pmpregions=8 --dtb=<dtb> -m0x80000000:0x8000000`（要確認） |

- シミュレーション上の Linux 起動は数億命令規模の見込み。カーネル設定を徹底的に絞る

---

## 9. 検証とベンチマーク

### 9.1 協調シミュレーション（方針 B: Spike を自前で接続。D-25）
- Spike をライブラリとしてリンクし、DUT が 1 命令確定するたびに 1 命令進めて比較
- 参考実装: Ibex の Spike 協調シミュレーション、CVA6 の Spike 並走
- 確定命令の記録（RVFI と XiangShan DiffTest の `DiffInstrCommit` の中間）:

```
order, pc, insn,
trap{valid, cause, tval}, intr{valid, cause},
rd{valid, addr, data},
mem{valid, addr, rmask, wmask, rdata, wdata},
csr{valid, addr, wdata},
mmio (Spike に値を注入する対象か)
```
- 毎命令比較: PC、命令、例外、レジスタ書き込み。CSR は書き込み時 + 一定間隔で主要 CSR 全体
- 注入: mcycle/time/instret、MMIO 読み出し値、割り込み
- DiffTest から借りる発想: ストアバッファ排出時・リフィル時の検査（`DiffSbufferEvent`/`DiffRefillEvent` 相当）、スナップショットからの自動再実行（LightSSS 相当）。最初は確定命令ログを常時出力し不一致直前を見る簡易版でよい

### 9.2 道具の段階
riscv-tests / riscv-arch-test → RISCV-DV（M1 から）→ キャッシュ単体のランダムテスト（参照メモリモデルと比較）→ SymbiYosys による AXI/ACE の形式検証（ZipCPU の AXI プロパティが参考）→ OpenSBI/Linux 起動 → FPGA 長時間試験 → マルチコアのリトマステスト

- リファレンスモデル: 最初は Spike。マルチコア段階でメモリモデル検査機能を持つ Whisper を検討
- テストベンチの単純メモリは待ち時間と ready/valid 遅延をランダム化（シードで再現）

### 9.3 FPGA でのデバッグ
- 直近の確定命令をリングバッファ（BRAM）に残し JTAG/UART で読み出す
- デバッグモジュール + OpenOCD + GDB
- ILA は信号の候補が絞れてから

### 9.4 ベンチマークと計測
- 比較用の数字: CoreMark（CoreMark/MHz）、Embench。**I$ や分岐予測の評価には向かない**
- フロントエンド評価: 命令の使用範囲が大きいワークロード（インタプリタ、gzip/xz、SQLite、Linux 起動時間）、I$ 容量スイープなどのマイクロベンチマーク
- Dhrystone は使わない
- 性能カウンタ（早めに）: サイクル、確定命令、I$/D$ ミス、iTLB/dTLB ミス、分岐ミス、BTB ミス、Fetch Buffer 空サイクル、D でのストールとその理由 → CPI の内訳
- Linux 上は SBI PMU 経由で `perf`
- 比較相手: 同じボード上の VexRiscv/NaxRiscv/Rocket、VisionFive 2（U74）
- 評価は「サイクルあたりの性能 × 達成周波数」
- RTL を書く前に Spike のキャッシュシミュレーション（`--ic`/`--dc`）で設計の選択肢を見積もる

---

## 10. M0 の作業項目

1. ディレクトリ構成と `.f` の階層（`core.f` / `soc.f` / `sim.f`）
2. SV パッケージ: 命令/パイプライン構造体、メモリインタフェース（REQ/RESP）、確定命令の記録
3. 7 段パイプラインの骨格（F0〜W、4B フェッチ、Fetch Buffer は命令単位の FIFO）
4. デコーダ（並べられる純粋な組み合わせ回路として書く）、レジスタファイル（ポート数パラメータ化）
5. スコアボード + フォワーディング、リダイレクトバス、フラッシュ規則
6. ALU、分岐、乗算器（3〜4 段）、除算器（反復）、LSU（AMO は read-modify-write、LR/SC 予約）
7. M-mode 最小 CSR、ECALL/MRET、未実装 CSR の例外
8. C++ 単純メモリ（DPI、ランダム待ち時間）、`tohost` 監視
9. Spike ライブラリとの比較
10. riscv-tests のビルドと実行スクリプト、CI
11. OOC 合成スクリプト（xc7a100t-1）と結果の記録

---

## 11. 周波数（200MHz = 5ns）で効く箇所

| 箇所 | 対策 |
|---|---|
| X: フォワード → 64bit ALU → フォワード | フォワード元を絞る、分岐比較を ALU と分ける |
| ロード結果の使用 | 2 サイクル後に使用可能 |
| 乗算 | DSP で 3〜4 段 |
| CSR 読み出し | 専用 1 サイクル |
| ストール信号のファンアウト | Fetch Buffer、スキッドバッファで区切る |
| F0 の次 PC | BTB はペナルティ 1 サイクル構成 |
| 分岐ミスのリダイレクト | 1 段レジスタ |
| ストアバッファ比較 | ページ内オフセットで先行比較 |

- 周波数目標が M3 までの進捗を止めないこと。ただしロード結果の使用サイクル数など構造に関わるものは最初から目標に合わせる

---

## 12. ツールとファイル管理（D-26, D-27）

- ファイル一覧の正は Verilator の `.f`（`CLAUDE.md` の規則を参照）
- Hog は FPGA 実装段階で導入。`core.f`/`soc.f` からリストファイルを生成、ボード固有ファイル（トップ、`.xdc`、`.xci`）は Hog 側のみ
- 生成したリストファイルは、再生成して差分がないことを CI 等で確認
- git のハッシュを SoC 情報レジスタ / mimpid から読めるようにする
- テクノロジーラッパー: `rtl/tech/generic`（動作記述）と `rtl/tech/xilinx`（XPM）を同名モジュールで用意
- 再利用候補: `pulp-platform/axi`、`riscv-dbg`、CVFPU（FP 追加時）。ライセンスを一覧で記録

---

## 13. FPGA とボード（D-24）

- 周波数追跡の合成対象: **xc7a100t-1**（M0 から）
- 実機ボードは riscv-tests 通過後に決定。候補:

| ボード | 特徴 | 注意 |
|---|---|---|
| xc7k325t-2 搭載ボード（手元） | 高速・大容量 | 無償版 Vivado では扱えないはず（ライセンス要確認） |
| KR260（Kria K26） | UltraScale+、Arm 側で Linux を動かして制御できる、PS に ACE-Lite の HPC ポート（128bit） | 7 シリーズの周波数目標とは比較不可。DDR は PS 経由で共有 |
| Arty A7-35T | 安価、200MHz 目標の対象 | 容量が小さく MIG 込みで窮屈な可能性 |
| Nexys A7（xc7a100t） | 余裕あり | DDR2 128MB |

- クロック: コアと MIG のクロック領域を分け AXI のクロック変換を挟む（7 シリーズの MIG ユーザークロックは 100MHz 前後が多い）
- リセット: valid ビットだけに配る、同期化回路を置く

---

## 14. 将来の拡張（D-28）

- 進め方: スコアボード + ノンブロッキングキャッシュ（hit-under-miss）→ インオーダー 2-way → （アウトオブオーダーは最後の選択肢）
- FPGA ではコアのクロックが低く DRAM 待ちがサイクル換算で小さいため、アウトオブオーダーの利益が薄い
- 2-way: 非対称パイプ（pipe0: ALU/分岐/LSU/CSR、pipe1: ALU）、LSU 1 本、単純なペアリング規則、古い命令を常に pipe0
- 今から仕込むこと: スコアボード、デコーダを並べられる形、レジスタファイルのポート数パラメータ化、Fetch Buffer 出口幅のパラメータ化
- 参考: SiFive U74、Cortex-A53/A55、NaxRiscv/VexRiscv

---

## 15. 参考資料

- XiangShan 設計文書（Kunminghu V3）: https://docs.xiangshan.cc/projects/design/
- XiangShan DiffTest: https://github.com/OpenXiangShan/difftest
- Chips and Cheese: SiFive P870 / P550、Ventana Veyron V1、T-Head C910 の解説記事
- OpenSBI Platform Requirements（`docs/platform_requirements.md`）
- Hog: https://hog.readthedocs.io/
- VexRiscv（FPGA 上の周波数の参考値）: https://github.com/SpinalHDL/VexRiscv
- Whisper: https://github.com/tenstorrent/whisper
