#pragma once
#include <stdint.h>
#include "../include/bootinfo.h"

const char SCANCODE_TO_ASCII[] = {
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    '\t',
    '`',
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    'q',
    '1',
    0x00,
    0x00,
    0x00,
    'z',
    's',
    'a',
    'w',
    '2',
    0x00,
    0x00,
    'c',
    'x',
    'd',
    'e',
    '4',
    '3',
    0x00,
    0x00,
    ' ',
    'v',
    'f',
    't',
    'r',
    '5',
    0x00,
    0x00,
    'n',
    'b',
    'h',
    'g',
    'y',
    '6',
    0x00,
    0x00,
    0x00,
    'm',
    'j',
    'u',
    '7',
    '8',
    0x00,
    0x00,
    ',',
    'k',
    'i',
    'o',
    '0',
    '9',
    0x00,
    0x00,
    '.',
    '/',
    'l',
    ';',
    'p',
    '-',
    0x00,
    0x00,
    0x00,
    '\'',
    0x00,
    '[',
    '=',
    0x00,
    0x00,
    0x00,
    '\n',
    ']',
    0x00,
    '\\',
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    '\b',
    0x00,
    0x00,
    '1',
    0x00,
    '4',
    '7',
    0x00,
    0x00,
    0x00,
    '0',
    '.',
    '2',
    '5',
    '6',
    '8',
    0x1B,
    0x00,
    0x00,
    '+',
    '3',
    '-',
    '*',
    '9',
    0x00,
    0x00,
};

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

    const uint8_t IRQ_MASTER_BASE = 0x20;
    const uint8_t IRQ_KEYBOARD = 1;
    const uint8_t IRQ_MOUSE = 12;

    const uint8_t VEC_APIC = 0x40;
    const uint8_t VEC_MOUSE = 0x2C;

    void init(const BootInfo *bi); // IDT構築 & lidt

    void enable_breakpoint(bool on); // 必要なら int3 を使うときだけ
    void install_double_fault(uint8_t ist_index);

} // namespace idt
