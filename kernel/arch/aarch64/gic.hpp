#pragma once

#include <stdint.h>

namespace Arch
{
namespace AArch64
{
namespace GIC
{

// QEMU 'virt' machine memory map
constexpr uint64_t kGicDistributorBase = 0x08000000;
constexpr uint64_t kGicCpuInterfaceBase = 0x08010000;

// GIC Distributor Registers
struct GicDistributor
{
    volatile uint32_t CTLR;        // 0x000 Control Register
    volatile uint32_t TYPER;       // 0x004 Interrupt Controller Type Register
    volatile uint32_t IIDR;        // 0x008 Implementer Identification Register
    uint32_t Reserved0[29];        // 0x00C-0x080
    volatile uint32_t IGROUPR[32]; // 0x080 Interrupt Group Registers
    volatile uint32_t ISENABLER[32]; // 0x100 Interrupt Set-Enable Registers
    volatile uint32_t ICENABLER[32]; // 0x180 Interrupt Clear-Enable Registers
    volatile uint32_t ISPENDR[32];   // 0x200 Interrupt Set-Pending Registers
    volatile uint32_t ICPENDR[32];   // 0x280 Interrupt Clear-Pending Registers
    volatile uint32_t ISACTIVER[32]; // 0x300 Interrupt Set-Active Registers
    volatile uint32_t ICACTIVER[32]; // 0x380 Interrupt Clear-Active Registers
    volatile uint32_t IPRIORITYR[255]; // 0x400 Interrupt Priority Registers
    volatile uint32_t
        ITARGETSR[255];          // 0x800 Interrupt Processor Targets Registers
    volatile uint32_t ICFGR[64]; // 0xC00 Interrupt Configuration Registers
    // ... others omitted for simplicity
};

// GIC CPU Interface Registers
struct GicCpuInterface
{
    volatile uint32_t CTLR; // 0x000 Control Register
    volatile uint32_t PMR;  // 0x004 Priority Mask Register
    volatile uint32_t BPR;  // 0x008 Binary Point Register
    volatile uint32_t IAR;  // 0x00C Interrupt Acknowledge Register
    volatile uint32_t EOIR; // 0x010 End of Interrupt Register
    volatile uint32_t RPR;  // 0x014 Running Priority Register
    volatile uint32_t
        HPPIR; // 0x018 Highest Priority Pending Interrupt Register
    // ... others omitted
};

void Initialize();
uint32_t AcknowledgeInterrupt();
void EndOfInterrupt(uint32_t interrupt_id);
void EnableInterrupt(uint32_t interrupt_id);

} // namespace GIC
} // namespace AArch64
} // namespace Arch
