#pragma once
#include <stdint.h>

struct NvmeRegs
{
    volatile uint64_t CAP;    // 0x00
    volatile uint32_t VS;     // 0x08
    volatile uint32_t INTMS;  // 0x0C
    volatile uint32_t INTMC;  // 0x10
    volatile uint32_t CC;     // 0x14
    uint32_t _rsv0;           // 0x18
    volatile uint32_t CSTS;   // 0x1C
    volatile uint32_t NSSR;   // 0x20 (optional)
    volatile uint32_t AQA;    // 0x24
    volatile uint32_t ASQ_LO; // 0x28
    volatile uint32_t ASQ_HI; // 0x2C
    volatile uint32_t ACQ_LO; // 0x30
    volatile uint32_t ACQ_HI; // 0x34
                              // ドアベルは BAR0 + 0x1000 + (4 << DSTRD) * (2*qid + 0/1)
};