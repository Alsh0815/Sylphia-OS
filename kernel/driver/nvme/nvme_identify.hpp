#pragma once
#include <stddef.h>
#include <stdint.h>

namespace NVMe
{

    struct IdentifyControllerData
    {
        uint16_t vid;           // 0-1: PCI Vendor ID
        uint16_t ssvid;         // 2-3: PCI Subsystem Vendor ID
        char sn[20];            // 4-23: Serial Number (ASCII)
        char mn[40];            // 24-63: Model Number (ASCII)
        char fr[8];             // 64-71: Firmware Revision
        uint8_t rab;            // 72: Recommended Arbitration Burst
        uint8_t ieee[3];        // 73-75: IEEE OUI Identifier
        uint8_t cmic;           // 76: Controller Multi-Path I/O and Namespace Sharing Capabilities
        uint8_t mdts;           // 77: Maximum Data Transfer Size
        uint16_t cntlid;        // 78-79: Controller ID
        uint32_t ver;           // 80-83: Version
        uint32_t rtd3r;         // 84-87: RTD3 Resume Latency
        uint32_t rtd3e;         // 88-91: RTD3 Entry Latency
        uint32_t oaes;          // 92-95: OAES
        uint8_t reserved[3996]; // 96-4095: 残りは省略
    } __attribute__((packed));

    struct IdentifyNamespaceData
    {
        uint64_t nsze;         // 0-7: Namespace Size (総セクタ数)
        uint64_t ncap;         // 8-15: Namespace Capacity
        uint64_t nuse;         // 16-23: Namespace Utilization
        uint8_t nsfeat;        // 24: Namespace Features
        uint8_t nlbaf;         // 25: Number of LBA Formats
        uint8_t flbas;         // 26: Formatted LBA Size
        uint8_t reserved[101]; // 簡易パディング

        // LBA Format Data (16個分ある)
        // ここにセクタサイズ情報が入っている
        struct LBAFormat
        {
            uint16_t ms; // Metadata Size
            uint8_t ds;  // LBA Data Size (2の乗数。9なら512B, 12なら4KB)
            uint8_t rp;  // Relative Performance
        } lbaf[16];

        uint8_t reserved2[3904];
    } __attribute__((packed));

    static_assert(offsetof(IdentifyNamespaceData, flbas) == 26, "Offset mismatch: flbas");
    static_assert(offsetof(IdentifyNamespaceData, lbaf) == 128, "Offset mismatch: lbaf (Padding size incorrect!)");
    static_assert(sizeof(IdentifyNamespaceData) == 4096, "Size mismatch: IdentifyNamespaceData must be 4096 bytes");

}