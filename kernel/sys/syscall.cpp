#include "syscall.hpp"
#include <stdint.h>
#include "app/elf/elf_loader.hpp"
#include "fs/fat32/fat32_driver.hpp"
#include "memory/memory_manager.hpp"
#include "printk.hpp"

// 定数定義 (MSR)
const uint32_t kMSR_EFER = 0xC0000080;
const uint32_t kMSR_STAR = 0xC0000081;
const uint32_t kMSR_LSTAR = 0xC0000082;
const uint32_t kMSR_FMASK = 0xC0000084;
const uint32_t kMSR_KERNEL_GS_BASE = 0xC0000102;

// asmfunc.asm
extern "C" void ExitApp();
extern "C" uint64_t ReadMSR(uint32_t msr);
extern "C" void WriteMSR(uint32_t msr, uint64_t value);
extern "C" void SyscallEntry();

// コンテキストの実体
SyscallContext *g_syscall_context = nullptr;

// ■ C++側システムコールハンドラ
// アセンブリ側から呼び出される
extern "C" uint64_t SyscallHandler(
    uint64_t syscall_number,
    uint64_t arg1,
    uint64_t arg2,
    uint64_t arg3,
    uint64_t arg4,
    uint64_t return_rip)
{
    switch (syscall_number)
    {
    case 1: // PutChar
        kprintf("%c", static_cast<char>(arg1));
        return 0;

    case 2: // Exit
        kprintf("\n[Kernel] App Exited via Syscall.\n");

        // ★ 修正: 安全にコンテキストを復元して戻る関数を呼ぶ
        ExitApp();

        // ここには戻ってこない
        while (1)
            __asm__ volatile("hlt");
        return 0;

    case 3: // ListDirectory (ls)
        // arg1: cluster (0=Root)
        if (FileSystem::g_fat32_driver)
        {
            FileSystem::g_fat32_driver->ListDirectory(static_cast<uint32_t>(arg1));
        }
        return 0;

    case 4: // ReadFile
        // arg1: filename (char*)
        // arg2: buffer (void*)
        // arg3: buffer_size (uint32_t)
        // 戻り値: 読み込んだバイト数 (uint64_t)
        if (FileSystem::g_fat32_driver)
        {
            const char *name = reinterpret_cast<const char *>(arg1);
            void *buf = reinterpret_cast<void *>(arg2);
            uint32_t len = static_cast<uint32_t>(arg3);
            return FileSystem::g_fat32_driver->ReadFile(name, buf, len);
        }
        return 0;

    default:
        kprintf("Unknown Syscall: %ld\n", syscall_number);
        return 0;
    }
}

void InitializeSyscall()
{
    // 0. コンテキスト領域の確保
    g_syscall_context = new SyscallContext();

    // 1. EFER.SCE (System Call Enable) ビットを有効化
    // Bit 0: SCE
    const uint64_t kEFER_SCE = 1;
    uint64_t efer = ReadMSR(kMSR_EFER);
    efer |= kEFER_SCE;
    WriteMSR(kMSR_EFER, efer);

    // 2. システムコール専用カーネルスタックの確保 (16KB)
    const size_t kStackSize = 16 * 1024;
    void *stack_mem = MemoryManager::Allocate(kStackSize);

    // スタックは高位アドレスから低位へ伸びるため、末尾をセット
    g_syscall_context->kernel_stack_ptr = reinterpret_cast<uint64_t>(stack_mem) + kStackSize;

    // 3. MSRの設定

    // STAR: セグメント設定
    // uint64_t star = (static_cast<uint64_t>(0x08) << 32) | (static_cast<uint64_t>(0x1B) << 48);
    uint64_t star = (static_cast<uint64_t>(0x08) << 32) | (static_cast<uint64_t>(0x18) << 48);
    WriteMSR(kMSR_STAR, star);

    // LSTAR: エントリポイント
    WriteMSR(kMSR_LSTAR, reinterpret_cast<uint64_t>(SyscallEntry));

    // FMASK: RFLAGSマスク
    // 割り込み禁止(IF=0x200)をマスクして、syscall中は割り込み禁止にする
    WriteMSR(kMSR_FMASK, 0x200);

    // KERNEL_GS_BASE: コンテキスト構造体のアドレス
    WriteMSR(kMSR_KERNEL_GS_BASE, reinterpret_cast<uint64_t>(g_syscall_context));

    kprintf("[Syscall] Initialized. Context at %lx\n", g_syscall_context);

    uint64_t current_star = ReadMSR(kMSR_STAR);
    kprintf("[Syscall] MSR_STAR set to: %lx\n", current_star);
}