#include <stdint.h>

#if defined(__x86_64__)

#include "cxx.hpp"
#include "segmentation.hpp"
#include "x86_descriptor.hpp"


extern "C" void LoadGDT(uint16_t limit, uint64_t offset);
extern "C" void SetDSAll(uint16_t value);
extern "C" void LoadTR(uint16_t sel);

uint64_t gdt[8];
TSS64 tss;

// GDTエントリを作るヘルパー
uint64_t MakeSegmentDescriptor(uint32_t type,
                               uint32_t descriptor_privilege_level)
{
    uint64_t desc = 0;
    // Code/Data Segment Descriptor (System=1)
    desc |= static_cast<uint64_t>(type) << 40;
    desc |= static_cast<uint64_t>(descriptor_privilege_level) << 45;
    desc |= static_cast<uint64_t>(1) << 47; // Present
    desc |= static_cast<uint64_t>(1) << 44; // System Segment (1 for Code/Data)
    desc |= static_cast<uint64_t>(1) << 53; // Long Mode (L)
    // D/B bit is 0 for 64-bit code
    return desc;
}

// データセグメント用 (Execute Disable等)
uint64_t MakeDataSegmentDescriptor(uint32_t descriptor_privilege_level)
{
    uint64_t desc = 0;
    desc |= 0xFFFF;
    desc |= static_cast<uint64_t>(2) << 40;
    desc |= static_cast<uint64_t>(descriptor_privilege_level) << 45;
    desc |= static_cast<uint64_t>(1) << 47; // Present
    desc |= static_cast<uint64_t>(1) << 44; // System Segment
    desc |= static_cast<uint64_t>(0xF) << 48;
    desc |= static_cast<uint64_t>(1) << 55;
    // desc |= static_cast<uint64_t>(1) << 54;
    return desc;
}

void SetTSSDescriptor(int index, uint64_t base, uint32_t limit)
{
    // TSS Descriptor は System Segment (S=0) なので構造が特殊
    // かつ 16バイト (2エントリ分) を使う

    // Low 8 bytes
    uint64_t low = 0;
    low |= (limit & 0xFFFF);
    low |= (base & 0xFFFF) << 16;
    low |= ((base >> 16) & 0xFF) << 32;
    low |= (uint64_t)0b1001 << 40; // Type=9 (Available 64-bit TSS)
    low |= (uint64_t)0 << 44;      // System Segment (0)
    low |= (uint64_t)0 << 45;      // DPL=0 (TSS自体はカーネルだけが触る)
    low |= (uint64_t)1 << 47;      // Present
    low |= (uint64_t)((limit >> 16) & 0xF) << 48;
    low |= ((base >> 24) & 0xFF) << 56;

    // High 8 bytes
    uint64_t high = 0;
    high |= (base >> 32);

    gdt[index] = low;
    gdt[index + 1] = high;
}

void SetupSegments()
{
    // 0: Null
    gdt[0] = 0;

    // 1: Kernel CS (Type=10: Execute/Read, DPL=0)
    gdt[1] = MakeSegmentDescriptor(10, 0);

    // 2: Kernel DS (Type=2: Read/Write, DPL=0)
    gdt[2] = MakeDataSegmentDescriptor(0);

    // 3: User DS (32bit dummy)
    gdt[3] = 0x00cffa000000ffff;

    // 4: User DS (64bit) (Type=2: Read/Write, DPL=3)
    gdt[4] = MakeDataSegmentDescriptor(3);

    // 5: User CS (64bit) (Type=10: Execute/Read, DPL=3)
    gdt[5] = MakeSegmentDescriptor(10, 3);

    // TSSの初期化
    memset(&tss, 0, sizeof(tss));
    // IO Map BaseをTSSサイズの以上に設定してIO許可ビットマップを無効化
    tss.iomap_base = sizeof(tss);

    // 6 & 7: TSS Descriptor
    SetTSSDescriptor(6, reinterpret_cast<uint64_t>(&tss), sizeof(tss) - 1);

    // GDTロード
    LoadGDT(sizeof(gdt) - 1, reinterpret_cast<uint64_t>(&gdt[0]));

    // セグメントレジスタ更新
    SetDSAll(0); // Null Selector (x64ではDS/ES/FS/GSは0で良い)

    // Task Register ロード
    LoadTR(kTSS);
}

void SetKernelStack(uint64_t stack_addr)
{
    tss.rsp0 = stack_addr;
}

#else // AArch64

// AArch64: セグメンテーションは存在しないので空実装を提供
void SetupSegments() {}
void SetKernelStack(uint64_t stack_addr)
{
    (void)stack_addr;
}

#endif // defined(__x86_64__)