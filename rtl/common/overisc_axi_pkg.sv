// SPDX-License-Identifier: CERN-OHL-P-2.0
// AXI4 / AXI4-Lite の構造体（コア最上位の 3 ポート（D-34）と SoC で使う）
// コアのパッケージには依存しない
// フィールドの順序は pulp-platform/axi の typedef.svh に倣ったが、互換性は要件にしない（D-36）
// ACE（段階 2）で AW/AR にフィールドを足し、AC/CR/CD チャネルと RACK/WACK を追加する
package overisc_axi_pkg;

  // ---- 幅 ----
  localparam int AXI_ADDR_W = 56;  // コアの物理アドレス幅（overisc_cfg_pkg::PADDR_W）以上にする
  localparam int AXI_DATA_W = 128;  // D-17
  localparam int AXI_STRB_W = AXI_DATA_W / 8;
  localparam int AXI_ID_W = 4;
  localparam int AXI_USER_W  = 1;    // pulp-platform/axi に合わせて 1 以上（未使用は 0 固定）

  localparam int AXIL_ADDR_W = 56;
  localparam int AXIL_DATA_W = 64;  // コアからの MMIO アクセスは最大 8B
  localparam int AXIL_STRB_W = AXIL_DATA_W / 8;

  // ---- フィールドの型（AXI 仕様の固定幅） ----
  typedef logic [7:0] axi_len_t;
  typedef logic [2:0] axi_size_t;
  typedef logic [3:0] axi_cache_t;
  typedef logic [2:0] axi_prot_t;
  typedef logic [3:0] axi_qos_t;
  typedef logic [3:0] axi_region_t;
  typedef logic [5:0] axi_atop_t;  // AXI5 のアトミック。使わない（0 固定）

  typedef enum logic [1:0] {
    AXI_BURST_FIXED = 2'b00,
    AXI_BURST_INCR  = 2'b01,
    AXI_BURST_WRAP  = 2'b10
  } axi_burst_t;

  typedef enum logic [1:0] {
    AXI_RESP_OKAY   = 2'b00,
    AXI_RESP_EXOKAY = 2'b01,
    AXI_RESP_SLVERR = 2'b10,
    AXI_RESP_DECERR = 2'b11
  } axi_resp_code_t;

  typedef logic [AXI_ADDR_W-1:0] axi_addr_t;
  typedef logic [AXI_DATA_W-1:0] axi_data_t;
  typedef logic [AXI_STRB_W-1:0] axi_strb_t;
  typedef logic [AXI_ID_W-1:0] axi_id_t;
  typedef logic [AXI_USER_W-1:0] axi_user_t;

  typedef logic [AXIL_ADDR_W-1:0] axil_addr_t;
  typedef logic [AXIL_DATA_W-1:0] axil_data_t;
  typedef logic [AXIL_STRB_W-1:0] axil_strb_t;

  // ---- AXI4（命令側、データ側のキャッシュ側） ----
  typedef struct packed {
    axi_id_t     id;
    axi_addr_t   addr;
    axi_len_t    len;
    axi_size_t   size;
    axi_burst_t  burst;
    logic        lock;
    axi_cache_t  cache;
    axi_prot_t   prot;
    axi_qos_t    qos;
    axi_region_t region;
    axi_atop_t   atop;
    axi_user_t   user;
  } axi_aw_chan_t;

  typedef struct packed {
    axi_data_t data;
    axi_strb_t strb;
    logic      last;
    axi_user_t user;
  } axi_w_chan_t;

  typedef struct packed {
    axi_id_t        id;
    axi_resp_code_t resp;
    axi_user_t      user;
  } axi_b_chan_t;

  typedef struct packed {
    axi_id_t     id;
    axi_addr_t   addr;
    axi_len_t    len;
    axi_size_t   size;
    axi_burst_t  burst;
    logic        lock;
    axi_cache_t  cache;
    axi_prot_t   prot;
    axi_qos_t    qos;
    axi_region_t region;
    axi_user_t   user;
  } axi_ar_chan_t;

  typedef struct packed {
    axi_id_t        id;
    axi_data_t      data;
    axi_resp_code_t resp;
    logic           last;
    axi_user_t      user;
  } axi_r_chan_t;

  // マスタ → スレーブ
  typedef struct packed {
    axi_aw_chan_t aw;
    logic         aw_valid;
    axi_w_chan_t  w;
    logic         w_valid;
    logic         b_ready;
    axi_ar_chan_t ar;
    logic         ar_valid;
    logic         r_ready;
  } axi_req_t;

  // スレーブ → マスタ
  typedef struct packed {
    logic        aw_ready;
    logic        ar_ready;
    logic        w_ready;
    logic        b_valid;
    axi_b_chan_t b;
    logic        r_valid;
    axi_r_chan_t r;
  } axi_resp_t;

  // ---- AXI4-Lite（データ側の MMIO 側） ----
  typedef struct packed {
    axil_addr_t addr;
    axi_prot_t  prot;
  } axil_aw_chan_t;

  typedef struct packed {
    axil_data_t data;
    axil_strb_t strb;
  } axil_w_chan_t;

  typedef struct packed {axi_resp_code_t resp;} axil_b_chan_t;

  typedef struct packed {
    axil_addr_t addr;
    axi_prot_t  prot;
  } axil_ar_chan_t;

  typedef struct packed {
    axil_data_t data;
    axi_resp_code_t resp;
  } axil_r_chan_t;

  typedef struct packed {
    axil_aw_chan_t aw;
    logic          aw_valid;
    axil_w_chan_t  w;
    logic          w_valid;
    logic          b_ready;
    axil_ar_chan_t ar;
    logic          ar_valid;
    logic          r_ready;
  } axil_req_t;

  typedef struct packed {
    logic         aw_ready;
    logic         w_ready;
    axil_b_chan_t b;
    logic         b_valid;
    logic         ar_ready;
    axil_r_chan_t r;
    logic         r_valid;
  } axil_resp_t;

endpackage
