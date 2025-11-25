#include "segmentation.hpp"
#include "x86_descriptor.hpp"

// GDT本体 (配列として確保)
// 0: Null, 1: Kernel Code, 2: Kernel Data
SegmentDescriptor gdt[3];

// GDTレジスタ (lgdt命令に渡す構造体)
struct GDTRegister
{
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

// セグメント記述子をセットするヘルパー
void SetCodeSegment(SegmentDescriptor &desc, uint64_t type, uint64_t dpl)
{
    desc.data = 0;
    desc.bits.type = type;
    desc.bits.system_segment = 1; // 1: Code/Data Segment
    desc.bits.descriptor_privilege_level = dpl;
    desc.bits.present = 1;
    desc.bits.long_mode = 1;              // 64bit
    desc.bits.default_operation_size = 0; // 64bit code segmentでは0にする決まり
    desc.bits.granularity = 1;
}

void SetDataSegment(SegmentDescriptor &desc, uint64_t type, uint64_t dpl)
{
    SetCodeSegment(desc, type, dpl);
    desc.bits.long_mode = 0;
    desc.bits.default_operation_size = 1; // 32bit protected modeなど
}

// アセンブリ命令のラッパー
// GDTをロードし、セグメントレジスタ(CS, DS, SS...)をリロードする
void LoadGDT(uint16_t limit, uint64_t base)
{
    GDTRegister gdtr;
    gdtr.limit = limit;
    gdtr.base = base;

    // lgdt命令: ここは %0 (変数参照) なので % は1つでOK
    __asm__ volatile("lgdt %0" ::"m"(gdtr));

    // セグメントレジスタの更新
    // ここは拡張アセンブラ構文なので、レジスタの % は %% にエスケープ必須
    __asm__ volatile(
        "movw $0x10, %%ax\n" // %ax -> %%ax
        "movw %%ax, %%ds\n"  // %ds -> %%ds
        "movw %%ax, %%es\n"
        "movw %%ax, %%fs\n"
        "movw %%ax, %%gs\n"
        "movw %%ax, %%ss\n"

        "pushq $0x08\n"
        "leaq .next_label(%%rip), %%rax\n" // %rip -> %%rip, %rax -> %%rax
        "pushq %%rax\n"
        "lretq\n"
        ".next_label:\n"
        : : : "rax");
}

void SetupSegments()
{
    // 0: Null Descriptor (必須)
    gdt[0].data = 0;

    // 1: Kernel Code Segment
    // Type=10 (Execute/Read), DPL=0 (Ring0/Kernel)
    SetCodeSegment(gdt[1], 10, 0);

    // 2: Kernel Data Segment
    // Type=2 (Read/Write), DPL=0 (Ring0/Kernel)
    SetDataSegment(gdt[2], 2, 0);

    // CPUにロード (サイズ-1 を渡すのが仕様)
    LoadGDT(sizeof(gdt) - 1, (uint64_t)&gdt[0]);
}