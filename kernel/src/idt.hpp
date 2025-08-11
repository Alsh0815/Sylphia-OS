#pragma once
#include <stdint.h>
#include "../include/bootinfo.h"

namespace idt
{

    // 例外番号
    enum : uint8_t
    {
        VEC_DE = 0,  // #DE Divide-by-zero
        VEC_NMI = 2, // #NMI
        VEC_BP = 3,  // #BP Breakpoint
        VEC_UD = 6,  // #UD Invalid opcode
        VEC_DF = 8,  // #DF Double fault
        VEC_GP = 13, // #GP General protection fault
        VEC_PF = 14, // #PF Page fault
    };

    void init(const BootInfo *bi); // IDT構築 & lidt

    void enable_breakpoint(bool on); // 必要なら int3 を使うときだけ
    void install_double_fault(uint8_t ist_index);

} // namespace idt
