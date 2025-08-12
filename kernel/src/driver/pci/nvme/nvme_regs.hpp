#pragma once
#include <stdint.h>

// NVMe MMIO レジスタ（必要最小限）
struct NvmeRegs
{
    volatile uint64_t CAP;   // 0x00
    volatile uint32_t VS;    // 0x08
    volatile uint32_t INTMS; // 0x0C
    volatile uint32_t INTMC; // 0x10
    volatile uint32_t CC;    // 0x14
    uint32_t _rsv0;
    volatile uint32_t CSTS; // 0x1C
    uint32_t _rsv1;
    volatile uint32_t NSSR; // 0x20 (optional)
    uint32_t _rsv2;
    volatile uint32_t AQA; // 0x24
    volatile uint64_t ASQ; // 0x28
    volatile uint64_t ACQ; // 0x30
    // ドアベルは BAR0 + 0x1000 + (4 << DSTRD) * (2*qid + 0/1)
};
