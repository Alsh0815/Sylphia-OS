#include "sys/init/init.hpp"
#include "console.hpp"
#include "driver/usb/keyboard/keyboard.hpp"
#include "interrupt.hpp"
#include "memory/memory.hpp"
#include "memory/memory_manager.hpp"
#include "paging.hpp"
#include "pic.hpp"
#include "printk.hpp"
#include "segmentation.hpp"
#include "sys/logger/logger.hpp"
#include "sys/std/file_descriptor.hpp"
#include "sys/syscall.hpp"

extern "C" void EnableSSE();

// グローバルファイルディスクリプタ配列（main.cppから移動）
extern FileDescriptor *g_fds[16];

namespace Sys
{
namespace Init
{

void InitializeCore(const MemoryMap &memmap)
{
#if defined(__aarch64__)
    volatile char *uart = reinterpret_cast<volatile char *>(0x09000000);
    *uart = '[';
    *uart = 'A';
    *uart = ']'; // [A] InitializeCore entry
#endif

    SetupSegments();
#if defined(__aarch64__)
    *uart = '[';
    *uart = 'B';
    *uart = ']'; // [B] After SetupSegments
#endif

    SetupInterrupts();
#if defined(__aarch64__)
    *uart = '[';
    *uart = 'C';
    *uart = ']'; // [C] After SetupInterrupts
#endif

    DisablePIC();
#if defined(__aarch64__)
    *uart = '[';
    *uart = 'D';
    *uart = ']'; // [D] After DisablePIC
#endif

#if defined(__x86_64__) || defined(__aarch64__)
    EnableSSE();
#endif

    MemoryManager::Initialize(memmap);
#if defined(__aarch64__)
    *uart = '[';
    *uart = 'E';
    *uart = ']'; // [E] After MemoryManager
#endif

    const size_t kKernelStackSize = 1024 * 16; // 16KB
    void *kernel_stack = MemoryManager::Allocate(kKernelStackSize);
    uint64_t kernel_stack_end =
        reinterpret_cast<uint64_t>(kernel_stack) + kKernelStackSize;
    SetKernelStack(kernel_stack_end);
#if defined(__aarch64__)
    *uart = '[';
    *uart = 'F';
    *uart = ']'; // [F] After SetKernelStack
#endif

    kprintf("Kernel Stack setup complete at %lx\n", kernel_stack_end);

    // ページテーブル初期化 (x86_64/AArch64対応済み)
#if defined(__aarch64__)
    *uart = '[';
    *uart = 'G';
    *uart = ']'; // [G] Before PageManager
#endif
    kprintf("[Init] Calling PageManager::Initialize()...\n");
    PageManager::Initialize();
#if defined(__aarch64__)
    *uart = '[';
    *uart = 'H';
    *uart = ']'; // [H] After PageManager
#endif
    kprintf("[Init] Returned from PageManager::Initialize().\n");

    kprintf("[Init] Calling InitializeSyscall()...\n");
    InitializeSyscall();
    kprintf("[Init] Returned from InitializeSyscall().\n");
}

void InitializeIO()
{
    // 標準I/O初期化
    g_fds[0] = new KeyboardFD(); // Stdin
    g_fds[1] = new ConsoleFD();  // Stdout
    g_fds[2] = new ConsoleFD();  // Stderr
    kprintf("Standard I/O Initialized (FD 0, 1, 2).\n");

    // イベントロガー初期化
    Sys::Logger::InitializeLogger();
    kprintf("Event Logger Initialized.\n");
}

} // namespace Init
} // namespace Sys
