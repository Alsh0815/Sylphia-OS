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
    SetupSegments();
    SetupInterrupts();
    DisablePIC();
    EnableSSE();

    MemoryManager::Initialize(memmap);

    const size_t kKernelStackSize = 1024 * 16; // 16KB
    void *kernel_stack = MemoryManager::Allocate(kKernelStackSize);
    uint64_t kernel_stack_end =
        reinterpret_cast<uint64_t>(kernel_stack) + kKernelStackSize;
    SetKernelStack(kernel_stack_end);

    kprintf("Kernel Stack setup complete at %lx\n", kernel_stack_end);

    PageManager::Initialize();
    InitializeSyscall();
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
