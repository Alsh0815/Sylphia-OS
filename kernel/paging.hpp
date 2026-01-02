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
    // エントリタイプ (ヘルパー用)
    enum class Type
    {
        Table, // 次のレベルのテーブルを指す
        Page,  // 4KBページ
        Block  // 2MB/1GBページ (Huge Page)
    };

    uint64_t value;

#if defined(__x86_64__)
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
#elif defined(__aarch64__)
    struct
    {
        uint64_t valid : 1;      // [0] Valid (1=有効)
        uint64_t type : 1;       // [1] Descriptor Type (1=Table/Page, 0=Block)
        uint64_t attr_index : 3; // [4:2] AttrIndx (Memory Attributes Index)
        uint64_t ns : 1;         // [5] NS (Non-Secure)
        uint64_t ap : 2;         // [7:6] AP (Access Permissions)
        uint64_t sh : 2;         // [9:8] SH (Shareability)
        uint64_t af : 1;         // [10] AF (Access Flag)
        uint64_t ng : 1;         // [11] nG (not Global)
        uint64_t address : 36;   // [47:12] Output Address
        uint64_t reserved : 4;   // [51:48]
        uint64_t cont : 1;       // [52] Contiguous
        uint64_t pxn : 1;        // [53] PXN (Privileged Execute-never)
        uint64_t uxn : 1;        // [54] UXN (Unprivileged Execute-never)
        uint64_t ignored : 4;    // [58:55]
        uint64_t pbit : 4;       // [62:59] PBHA
        uint64_t ignored2 : 1;   // [63]
    } __attribute__((packed)) bits;
#endif

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

    bool IsPresent() const
    {
#if defined(__x86_64__)
        return bits.present;
#elif defined(__aarch64__)
        return bits.valid;
#endif
    }

    bool IsHugePage() const
    {
#if defined(__x86_64__)
        return bits.huge_page;
#elif defined(__aarch64__)
        // ValidかつType=0ならBlock(Huge)
        return bits.valid && (bits.type == 0);
#endif
    }

    // 汎用設定メソッド
    void Set(uint64_t addr, Type type, uint64_t flags)
    {
        value = 0; // 一旦クリア
        SetAddress(addr);

#if defined(__x86_64__)
        // x86_64
        // kPresent(1<<0), kWritable(1<<1), kUser(1<<2)
        // flagsはPageManager定義のビットマスクを想定

        bits.present = (flags & 1) ? 1 : 0;         // kPresent
        bits.read_write = (flags & 2) ? 1 : 0;      // kWritable
        bits.user_supervisor = (flags & 4) ? 1 : 0; // kUser

        if (type == Type::Block)
        {
            bits.huge_page = 1;
        }
#elif defined(__aarch64__)
        // AArch64
        bits.valid = (flags & 1) ? 1 : 0; // kPresent

        // Typeによる分岐
        if (type == Type::Table)
        {
            // Table Descriptor (Level 0, 1, 2)
            // bits[1] = 1 (Table)
            bits.type = 1;

            // Table DescriptorにはAP, SH,
            // AttrIndxなどは存在しない（または階層属性）
            // 誤って書き込むとCPUが異常動作するため、ここではtypeとvalid(上記)以外は0のままにする
        }
        else
        {
            // Block (Level 1, 2) or Page (Level 3)
            bits.type = (type == Type::Block) ? 0 : 1;

            // 属性・権限設定
            bits.af = 1; // Access Flag (Accessed)
            bits.sh = 3; // Inner Shareable

            // kDevice = 0x10 (PageManagerで定義)
            if (flags & 0x10)
            {
                bits.attr_index = 1; // Device-nGnRnE (MAIR[1])
            }
            else
            {
                bits.attr_index = 0; // Normal Memory (MAIR[0]想定)
            }

            // AP (Access Permissions)
            // KernelRW(00), RW(01), KernelRO(10), RO(11)
            bool writable = (flags & 2);
            bool user = (flags & 4);

            if (user)
            {
                bits.ap = writable ? 1 : 3; // 01(RW) or 11(RO)
            }
            else
            {
                bits.ap = writable ? 0 : 2; // 00(RW) or 10(RO)
            }
        }

        // Execute Never (とりあえずデータなら実行不可にする？一旦許可)
        // bits.uxn = 1;
        // bits.pxn = 1;
#endif
    }
    // 属性のコピー (HugePage分割時などに使用)
    void CopyAttributesFrom(const PageTableEntry &src)
    {
#if defined(__x86_64__)
        bits.present = 1; // コピー先は有効にする前提
        bits.read_write = src.bits.read_write;
        bits.user_supervisor = src.bits.user_supervisor;
        bits.write_through = src.bits.write_through;
        bits.cache_disable = src.bits.cache_disable;
        bits.global = src.bits.global;
        bits.no_execute = src.bits.no_execute;
        // huge_pageビットはコピーしない（分割目的なので）
#elif defined(__aarch64__)
        bits.valid = 1;
        bits.attr_index = src.bits.attr_index;
        bits.ns = src.bits.ns;
        bits.ap = src.bits.ap;
        bits.sh = src.bits.sh;
        bits.af = src.bits.af;
        bits.ng = src.bits.ng;
        bits.cont = src.bits.cont;
        bits.pxn = src.bits.pxn;
        bits.uxn = src.bits.uxn;
        // typeはコピーしない
#endif
    }

    // ユーザーアクセス許可を追加
    void SetUserAccess(bool enable)
    {
#if defined(__x86_64__)
        bits.user_supervisor = enable ? 1 : 0;
#elif defined(__aarch64__)
        // AP: 00(K_RW), 01(U_RW), 10(K_RO), 11(U_RO)
        // enableなら下位ビットを1にする(0x->1x)、disableなら0にする
        if (enable)
        {
            bits.ap |= 1;
        }
        else
        {
            bits.ap &= ~1;
        }
#endif
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
    static const uint64_t kDevice = 1 << 4; // AArch64: Device Memory

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

    // 指定されたメモリ領域のページ属性をDevice Memory (nGnRnE) に変更する
    // ptr: 領域の先頭アドレス (ページ境界を推奨)
    // size: 領域サイズ
    static void SetDeviceMemory(void *ptr, size_t size);

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