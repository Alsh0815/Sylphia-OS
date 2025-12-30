#pragma once
#include <stdint.h>

#if defined(__x86_64__)

// I/Oポートから8bit読む
static inline uint8_t IoIn8(uint16_t port)
{
    uint8_t data;
    __asm__ volatile("inb %1, %0" : "=a"(data) : "d"(port));
    return data;
}

// I/Oポートへ8bit書く
static inline void IoOut8(uint16_t port, uint8_t data)
{
    __asm__ volatile("outb %0, %1" : : "a"(data), "d"(port));
}

// 32bit
static inline uint32_t IoIn32(uint16_t port)
{
    uint32_t data;
    __asm__ volatile("inl %1, %0" : "=a"(data) : "d"(port));
    return data;
}

static inline void IoOut32(uint16_t port, uint32_t data)
{
    __asm__ volatile("outl %0, %1" : : "a"(data), "d"(port));
}

#else

// スタブ実装 (AArch64等、I/Oポートがないアーキテクチャ用)
static inline uint8_t IoIn8(uint16_t port)
{
    return 0;
}
static inline void IoOut8(uint16_t port, uint8_t data) {}
static inline uint32_t IoIn32(uint16_t port)
{
    return 0;
}
static inline void IoOut32(uint16_t port, uint32_t data) {}

#endif