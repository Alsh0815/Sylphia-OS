#pragma once
#include <stddef.h>
#include <stdint.h>

// ページサイズ
const uint64_t kPageSize4K = 4096;
const uint64_t kPageSize2M = 2 * 1024 * 1024;
const uint64_t kPageSize1G = 1 * 1024 * 1024 * 1024;

// ページテーブルエントリ (PTE) の構造
// 64ビット整数をビットフィールドとして扱います
union PageTableEntry
{
    uint64_t value;

    struct
    {
        uint64_t present
            : 1; // [0] 存在ビット (1=メモリにある, 0=ない/スワップ中)
        uint64_t read_write : 1; // [1] R/W (1=書き込み可, 0=読み取り専用)
        uint64_t user_supervisor
            : 1; // [2] U/S (1=ユーザー権限でもアクセス可, 0=カーネル専用)
        uint64_t write_through : 1; // [3] キャッシュ設定 (Write Through)
        uint64_t cache_disable : 1; // [4] キャッシュ無効化 (IO領域などで使用)
        uint64_t accessed
            : 1;            // [5] CPUがアクセスした時に1になる (アクセス統計用)
        uint64_t dirty : 1; // [6] CPUが書き込んだ時に1になる (スワップ判断用)
        uint64_t huge_page
            : 1; // [7] 1=ページサイズを大きくする (2MB/1GBページ)
        uint64_t global
            : 1; // [8] TLBフラッシュの対象外にする (カーネル領域など)
        uint64_t available : 3; // [11:9] OSが自由に使っていい領域
                                // (スワップ位置の記録などに使える)
        uint64_t address
            : 40; // [51:12]
                  // 次のテーブルまたは物理ページの物理アドレス(4KBアライメント)
        uint64_t available2 : 11; // [62:52] OS自由領域2
        uint64_t no_execute : 1;  // [63] NXビット (1=実行禁止)
    } __attribute__((packed)) bits;

    // アドレス部分だけをセット/ゲットするヘルパー
    void SetAddress(uint64_t addr)
    {
        // 下位12ビットと上位ビットをマスクしてセット
        bits.address = (addr >> 12) & 0xFFFFFFFFFF;
    }

    uint64_t GetAddress() const
    {
        return bits.address << 12;
    }
};

// 各テーブルは512個のエントリを持つ (4KBサイズに収まる)
struct PageTable
{
    PageTableEntry entries[512];
} __attribute__((aligned(4096)));

// 階層構造のエイリアス (実体は同じPageTable構造体)
using PML4Table = PageTable;     // Level 4 (最上位)
using PDPTable = PageTable;      // Level 3
using PageDirectory = PageTable; // Level 2

// CR3レジスタ (ページテーブルの場所をCPUに教えるレジスタ) 操作用
extern "C" void LoadCR3(uint64_t pml4_addr);
extern "C" uint64_t GetCR3();
extern "C" void InvalidateTLB(uint64_t virtual_addr);

class PageManager
{
  public:
    static const uint64_t kPresent = 1 << 0;
    static const uint64_t kWritable = 1 << 1;
    static const uint64_t kUser = 1 << 2;

    // ページングの初期化 (PML4の作成とアイデンティティマッピング)
    static void Initialize();

    // 仮想アドレスを物理アドレスにマップする
    // virtual_addr: 仮想アドレス (4KB整列)
    // physical_addr: 物理アドレス (4KB整列)
    // count: ページ数
    static void MapPage(uint64_t virtual_addr, uint64_t physical_addr,
                        size_t count = 1,
                        uint64_t flags = kPresent | kWritable);

    // 指定された仮想アドレス領域に、新しい物理フレームを割り当ててマップする
    // 成功したらtrue、メモリ不足などで失敗したらfalse
    static bool AllocateVirtual(uint64_t virtual_addr, size_t size,
                                uint64_t flags = kPresent | kWritable | kUser);

    // 新しいページテーブル領域を確保して初期化するヘルパー
    static PageTable *AllocateTable();

    // =========================================
    // プロセス用ページテーブル管理 (Ring 3対応)
    // =========================================

    // カーネルのPML4アドレスを取得
    static uint64_t GetKernelCR3();

    // プロセス専用のページテーブルを作成
    // カーネル領域（アイデンティティマッピング）をコピーし、
    // ユーザー領域は空のままにする
    // 戻り値: 新しいPML4の物理アドレス (CR3にロードする値)
    static uint64_t CreateProcessPageTable();

    // ページテーブルを切り替える
    // cr3_value: ロードするPML4の物理アドレス
    static void SwitchPageTable(uint64_t cr3_value);

    // プロセスのページテーブルを解放する
    // cr3_value: 解放するPML4の物理アドレス
    // 注意: カーネルのページテーブルは解放できない
    static void FreeProcessPageTable(uint64_t cr3_value);

    // 指定されたPML4を使って仮想メモリを確保（プロセス用）
    // target_cr3: 対象のPML4の物理アドレス
    static bool AllocateVirtualForProcess(uint64_t target_cr3,
                                          uint64_t virtual_addr, size_t size,
                                          uint64_t flags = kPresent |
                                                           kWritable | kUser);

    // ページテーブルをディープコピーする（指定階層のみ）
    // src: コピー元テーブル
    // level: 階層レベル (4=PML4, 3=PDP, 2=PD, 1=PT)
    static PageTable *CopyPageTable(PageTable *src, int level);

    // ページテーブル階層を解放する
    // table: 解放対象テーブル
    // level: 階層レベル
    // free_frames: 末端の物理フレームも解放するかどうか
    static void FreePageTableHierarchy(PageTable *table, int level,
                                       bool free_frames);

  private:
    static PML4Table *pml4_table_;
};