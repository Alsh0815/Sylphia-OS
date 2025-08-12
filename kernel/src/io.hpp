#pragma once
#include <stdint.h>

// 32bit I/O read/write（PCI config用）
static inline void outl(uint16_t port, uint32_t val)
{
    asm volatile("outl %0, %1" ::"a"(val), "Nd"(port));
}
static inline uint32_t inl(uint16_t port)
{
    uint32_t v;
    asm volatile("inl %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
