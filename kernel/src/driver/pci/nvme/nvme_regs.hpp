#pragma once
#include <cstddef>
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

static_assert(offsetof(NvmeRegs, CAP) == 0x00, "NVMe CAP offset mismatch");
static_assert(offsetof(NvmeRegs, VS) == 0x08, "NVMe VS offset mismatch");
static_assert(offsetof(NvmeRegs, INTMS) == 0x0C, "NVMe INTMS offset mismatch");
static_assert(offsetof(NvmeRegs, INTMC) == 0x10, "NVMe INTMC offset mismatch");
static_assert(offsetof(NvmeRegs, CC) == 0x14, "NVMe CC offset mismatch");
static_assert(offsetof(NvmeRegs, CSTS) == 0x1C, "NVMe CSTS offset mismatch");
static_assert(offsetof(NvmeRegs, NSSR) == 0x20, "NVMe NSSR offset mismatch");
static_assert(offsetof(NvmeRegs, AQA) == 0x24, "NVMe AQA offset mismatch");
static_assert(offsetof(NvmeRegs, ASQ_LO) == 0x28, "NVMe ASQ_LO offset mismatch");
static_assert(offsetof(NvmeRegs, ASQ_HI) == 0x2C, "NVMe ASQ_HI offset mismatch");
static_assert(offsetof(NvmeRegs, ACQ_LO) == 0x30, "NVMe ACQ_LO offset mismatch");
static_assert(offsetof(NvmeRegs, ACQ_HI) == 0x34, "NVMe ACQ_HI offset mismatch");
static_assert(sizeof(NvmeRegs) >= 0x38, "NVMe regs struct too small");

struct NvmeLbaFormat
{
    uint16_t ms;   // Metadata Size (bits 15:0)
    uint8_t lbads; // LBA Data Size (bits 23:16) = log2(bytes)
    uint8_t rp;    // Relative Performance (bits 25:24) in low 2 bits; upper 6 bits reserved
} __attribute__((packed));
static_assert(sizeof(NvmeLbaFormat) == 4, "LBAF entry must be 4 bytes");

struct NvmeIdentifyNamespace
{
    uint64_t nsze; // 0x00 Namespace Size (LBA count)
    uint64_t ncap; // 0x08 Namespace Capacity (LBA count)
    uint64_t nuse; // 0x10 Namespace Utilization (LBA count)

    uint8_t nsfeat; // 0x18 Namespace Features
    uint8_t nlbaf;  // 0x19 Number of LBA Formats (0-based)
    uint8_t flbas;  // 0x1A Formatted LBA Size
    uint8_t mc;     // 0x1B Metadata Capabilities
    uint8_t dpc;    // 0x1C Data Protection Capabilities
    uint8_t dps;    // 0x1D Data Protection Type Settings
    uint8_t nmic;   // 0x1E NMIC
    uint8_t rescap; // 0x1F Reservation Capabilities
    uint8_t fpi;    // 0x20 Format Progress Indicator
    uint8_t dlfeat; // 0x21 Deallocate Logical Block Features

    uint16_t nawun;  // 0x22 Namespace Atomic Write Unit Normal
    uint16_t nawupf; // 0x24 Namespace Atomic Write Unit Power Fail
    uint16_t nacwu;  // 0x26 Namespace Atomic Compare & Write Unit
    uint16_t nabsn;  // 0x28 Namespace Atomic Boundary Size Normal
    uint16_t nabo;   // 0x2A Namespace Atomic Boundary Offset
    uint16_t nabspf; // 0x2C Namespace Atomic Boundary Size Power Fail
    uint16_t noiob;  // 0x2E Namespace Optimal I/O Boundary

    uint8_t nvmcap[16]; // 0x30–0x3F NVM Capacity (128-bit, bytes)

    uint16_t npwg;      // 0x40 Namespace Preferred Write Granularity
    uint16_t npwa;      // 0x42 Namespace Preferred Write Alignment
    uint16_t npdg;      // 0x44 Namespace Preferred Deallocate Granularity
    uint16_t npda;      // 0x46 Namespace Preferred Deallocate Alignment
    uint16_t nows;      // 0x48 Namespace Optimal Write Size
    uint8_t rsvd74[18]; // 0x4A–0x5B Reserved

    uint32_t anagrpid; // 0x5C ANA Group Identifier
    uint8_t rsvd96[3]; // 0x60–0x62 Reserved
    uint8_t nsattr;    // 0x63 Namespace Attributes
    uint16_t nvmsetid; // 0x64–0x65 NVM Set Identifier
    uint16_t endgid;   // 0x66–0x67 Endurance Group Identifier
    uint8_t nguid[16]; // 0x68–0x77 Namespace GUID (big-endian per spec)
    uint8_t eui64[8];  // 0x78–0x7F IEEE EUI-64 (big-endian per spec)

    NvmeLbaFormat lbaf[16]; // 0x80–0xBF LBA Format 0..15

    uint8_t rsvd192[192];                // 0xC0–0x17F Reserved
    uint8_t vendor_specific[4096 - 384]; // 0x180–0xFFF Vendor Specific
} __attribute__((packed));

// ---- compile-time layout checks (excerpt) ----
static_assert(offsetof(NvmeIdentifyNamespace, nsze) == 0x00, "nsze offset");
static_assert(offsetof(NvmeIdentifyNamespace, ncap) == 0x08, "ncap offset");
static_assert(offsetof(NvmeIdentifyNamespace, nuse) == 0x10, "nuse offset");
static_assert(offsetof(NvmeIdentifyNamespace, nsfeat) == 0x18, "nsfeat offset");
static_assert(offsetof(NvmeIdentifyNamespace, nlbaf) == 0x19, "nlbaf offset");
static_assert(offsetof(NvmeIdentifyNamespace, flbas) == 0x1A, "flbas offset");
static_assert(offsetof(NvmeIdentifyNamespace, dps) == 0x1D, "dps offset");
static_assert(offsetof(NvmeIdentifyNamespace, nvmcap) == 0x30, "nvmcap offset");
static_assert(offsetof(NvmeIdentifyNamespace, lbaf) == 0x80, "lbaf offset");
static_assert(sizeof(NvmeIdentifyNamespace) == 4096, "Identify NS size");