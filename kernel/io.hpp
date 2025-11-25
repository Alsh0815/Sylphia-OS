#pragma once
#include <stdint.h>

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