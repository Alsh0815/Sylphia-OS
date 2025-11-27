#include "syscall.hpp"
#include <stdint.h>
#include "memory/memory_manager.hpp"
#include "printk.hpp"

// 定数定義 (MSR)
const uint32_t kMSR_STAR = 0xC0000081;
const uint32_t kMSR_LSTAR = 0xC0000082;
const uint32_t kMSR_FMASK = 0xC0000084;
const uint32_t kMSR_KERNEL_GS_BASE = 0xC0000102;

// asmfunc.asm
extern "C" void WriteMSR(uint32_t msr, uint64_t value);
extern "C" void SyscallEntry();

// コンテキストの実体
SyscallContext *g_syscall_context = nullptr;

// ■ C++側システムコールハンドラ
// アセンブリ側から呼び出される
extern "C" void SyscallHandler(uint64_t syscall_number, uint64_t arg1, uint64_t arg2, uint64_t arg3)
{
    // テスト用: 呼ばれたことをログに出す
    kprintf("SYSCALL Called! Num: %ld, Args: %ld, %ld, %ld\n", syscall_number, arg1, arg2, arg3);

    // ここで syscall_number に応じて処理を分岐
    // if (syscall_number == 1) { ... }
}

void InitializeSyscall()
{
    // 1. コンテキスト領域の確保
    g_syscall_context = new SyscallContext();

    // 2. システムコール専用カーネルスタックの確保 (16KB)
    const size_t kStackSize = 16 * 1024;
    void *stack_mem = MemoryManager::Allocate(kStackSize);

    // スタックは高位アドレスから低位へ伸びるため、末尾をセット
    g_syscall_context->kernel_stack_ptr = reinterpret_cast<uint64_t>(stack_mem) + kStackSize;

    // 3. MSRの設定

    // STAR: セグメント設定
    uint64_t star = (static_cast<uint64_t>(0x08) << 32) | (static_cast<uint64_t>(0x1B) << 48);
    WriteMSR(kMSR_STAR, star);

    // LSTAR: エントリポイント
    WriteMSR(kMSR_LSTAR, reinterpret_cast<uint64_t>(SyscallEntry));

    // FMASK: RFLAGSマスク
    // 割り込み禁止(IF=0x200)をマスクして、syscall中は割り込み禁止にする
    WriteMSR(kMSR_FMASK, 0x200);

    // KERNEL_GS_BASE: コンテキスト構造体のアドレス
    WriteMSR(kMSR_KERNEL_GS_BASE, reinterpret_cast<uint64_t>(g_syscall_context));

    kprintf("[Syscall] Initialized. Context at 0x%lx\n", g_syscall_context);
}