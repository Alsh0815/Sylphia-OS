#include "Debug.hpp"

namespace Debug
{
namespace Serial
{
#if defined(__x86_64__)
namespace
{
constexpr unsigned short COM1_PORT = 0x3F8;

inline void outb(unsigned short port, unsigned char value)
{
    asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

inline unsigned char inb(unsigned short port)
{
    unsigned char value;
    asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

inline bool IsTransmitEmpty()
{
    return (inb(COM1_PORT + 5) & 0x20) != 0;
}
} // namespace
#endif

void Out(const char c)
{
#if defined(__x86_64__)
    while (!IsTransmitEmpty())
        ;
    outb(COM1_PORT, static_cast<unsigned char>(c));
#endif
#if defined(__aarch64__)
    volatile char *uart = reinterpret_cast<volatile char *>(0x09000000);
    *uart = c;
#endif
}

void Out(const char *str)
{
#if defined(__x86_64__)
    while (*str)
    {
        while (!IsTransmitEmpty())
            ;
        outb(COM1_PORT, static_cast<unsigned char>(*str++));
    }
#endif
#if defined(__aarch64__)
    volatile char *uart = reinterpret_cast<volatile char *>(0x09000000);
    while (*str)
        *uart = *str++;
#endif
}
} // namespace Serial
} // namespace Debug