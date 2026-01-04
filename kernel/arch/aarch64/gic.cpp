#include "gic.hpp"
#include "Debug.hpp"

namespace Arch
{
namespace AArch64
{
namespace GIC
{

GicDistributor *g_gicd =
    reinterpret_cast<GicDistributor *>(kGicDistributorBase);
GicCpuInterface *g_gicc =
    reinterpret_cast<GicCpuInterface *>(kGicCpuInterfaceBase);

void Initialize()
{
    // 1. Disable Distributor
    g_gicd->CTLR = 0;

    // 2. Configure SPIs (Shared Peripheral Interrupts)
    // 今回は特に設定しないが、必要ならここで行う
    // 全てのSPIをGroup 0 (Secure/Kernel) に設定するなど

    // 3. Enable Distributor
    g_gicd->CTLR = 1; // Enable Group 0 (and Group 1 if GICv2)

    // 4. CPU Interface Configuration
    // Priority Mask: Allow all priorities (0xFF)
    g_gicc->PMR = 0xFF;

    // Binary Point: No sub-priority split
    g_gicc->BPR = 0;

    // Enable CPU Interface
    g_gicc->CTLR = 1; // Enable Signaling of Interrupts
}

uint32_t AcknowledgeInterrupt()
{
    return g_gicc->IAR;
}

void EndOfInterrupt(uint32_t interrupt_id)
{
    g_gicc->EOIR = interrupt_id;
}

void EnableInterrupt(uint32_t interrupt_id)
{
    // ISENABLER access
    // Register index = ID / 32
    // Bit index = ID % 32
    uint32_t reg_idx = interrupt_id / 32;
    uint32_t bit_idx = interrupt_id % 32;

    g_gicd->ISENABLER[reg_idx] = (1 << bit_idx);

    // PPI (ID < 32) の場合はICFGRの設定は固定またはReadOnlyの場合が多いが、
    // 必要ならLevel/Edge設定を行う (TimerはLevel Sensitive)
    // 2ビット/IRQなので、ICFGR[n] は16個のIRQ設定を持つ
    // INTID 30 -> ICFGR[1], bit 28-29
    // QEMU virt timerはActive-LOW Level
    // sensitiveだがGIC上では構成済みであることが多い
}

} // namespace GIC
} // namespace AArch64
} // namespace Arch
