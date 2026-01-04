#include "timer_arch.hpp"
#include "Debug.hpp"
#include "gic.hpp"

namespace Arch
{
namespace AArch64
{
namespace Timer
{

uint64_t g_ticks_per_ms = 0;

void Initialize()
{
    // Read counter frequency
    uint64_t frq;
    asm volatile("mrs %0, cntfrq_el0" : "=r"(frq));

    g_ticks_per_ms = frq / 1000;

    // Enable Timer Interrupt in GIC (INTID 30: Physical Timer)
    GIC::EnableInterrupt(30);

    // Disable Virtual Timer (INTID 27) explicitly to avoid interrupt storm
    // cntv_ctl_el0 = 0
    asm volatile("msr cntv_ctl_el0, %0" ::"r"((uint64_t)0));
    // Clear Virtual Timer Interrupt Status just in case
    asm volatile("msr cntv_tval_el0, %0" ::"r"((uint64_t)0));
}

void SetIntervalMs(uint32_t ms)
{
    uint64_t count = g_ticks_per_ms * ms;

    // Set Timer Value (down counter)
    asm volatile("msr cntp_tval_el0, %0" ::"r"(count));
}

void Enable()
{
    // Enable Timer (Bit 0) and Unmask Interrupt (Bit 1 = 0)
    uint64_t ctl = 1;
    asm volatile("msr cntp_ctl_el0, %0" ::"r"(ctl));
}

void Disable()
{
    // Disable Timer
    uint64_t ctl = 0;
    asm volatile("msr cntp_ctl_el0, %0" ::"r"(ctl));
}

} // namespace Timer
} // namespace AArch64
} // namespace Arch
