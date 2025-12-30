#include <stdint.h>

#if defined(__x86_64__)

// I/Oポート操作用のアセンブリラッパー (インライン関数)
static inline void IoOut8(uint16_t port, uint8_t data)
{
    __asm__ volatile("outb %0, %1" : : "a"(data), "d"(port));
}

static inline uint8_t IoIn8(uint16_t port)
{
    uint8_t data;
    __asm__ volatile("inb %1, %0" : "=a"(data) : "d"(port));
    return data;
}

void DisablePIC()
{
    // マスターPICとスレーブPICの全ての割り込みをマスク(禁止)する
    // データポート: Master=0x21, Slave=0xA1
    // 全ビットを1にするとマスクされる
    IoOut8(0xA1, 0xFF);
    IoOut8(0x21, 0xFF);
}

#else

void DisablePIC()
{
    // AArch64 has no legacy PIC (8259)
}

#endif