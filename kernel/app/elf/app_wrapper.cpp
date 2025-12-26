#include "app_wrapper.hpp"
#include "elf_loader.hpp"
#include "paging.hpp"
#include "printk.hpp"
#include "segmentation.hpp"
#include "task/scheduler.hpp"
#include "task/task_manager.hpp"
#include <std/string.hpp>

// asmfunc.asmで定義
extern "C" void EnterUserMode(uint64_t entry_point, uint64_t user_stack_top,
                              int argc, uint64_t argv_ptr);
extern "C" void WriteMSR(uint32_t msr, uint64_t value);

// syscall.cppで定義
#include "sys/syscall.hpp"
extern SyscallContext *g_syscall_context;

// MSR定数
const uint32_t kMSR_GS_BASE = 0xC0000101;
const uint32_t kMSR_KERNEL_GS_BASE = 0xC0000102;

// アプリタスクのエントリーポイント
// タスクが起動されると、この関数が呼ばれる
void AppTaskEntry()
{
    Task *current = TaskManager::GetCurrentTask();
    if (!current || !current->is_app)
    {
        kprintf("[AppTask] Error: Not an app task!\n");
        // タスクを終了
        TaskManager::TerminateTask(current);
        TaskManager::SetCurrentTask(nullptr);
        Scheduler::Schedule();
        return;
    }

    kprintf("[AppTask] Starting '%s' (ID=%lu) in Ring 3\n", current->name,
            current->task_id);

    // プロセスのページテーブルに切り替え（ユーザースタックへのアクセスに必要）
    PageManager::SwitchPageTable(current->context.cr3);

    // ユーザースタックにargvを配置
    uint64_t sp = current->user_stack_top;
    uint64_t user_argv_ptrs[32];

    // 引数文字列をユーザースタックにコピー
    for (int i = 0; i < current->argc; ++i)
    {
        int len = strlen(current->argv[i]) + 1;
        sp -= len;
        // プロセスのページテーブル上で書き込み
        strcpy(reinterpret_cast<char *>(sp), current->argv[i]);
        user_argv_ptrs[i] = sp;
    }

    // 16バイトアライメント
    sp = sp & ~0xFULL;

    // NULL終端
    sp -= 8;
    *reinterpret_cast<uint64_t *>(sp) = 0;

    // argvポインタ配列をスタックに積む
    for (int i = current->argc - 1; i >= 0; --i)
    {
        sp -= 8;
        *reinterpret_cast<uint64_t *>(sp) = user_argv_ptrs[i];
    }

    uint64_t argv_ptr = sp;

    kprintf("[AppTask] Entering user mode: Entry=%lx, SP=%lx, argc=%d\n",
            current->entry_point, sp, current->argc);
    kprintf("[AppTask] argv_ptr=%lx, CR3=%lx\n", argv_ptr,
            current->context.cr3);

    // アプリ実行中フラグを設定（シェルへのキー入力を抑制）
    g_app_running = true;

    kprintf("[AppTask] About to call EnterUserMode NOW!\n");

    // 重要: ユーザーモードから戻ったときに使用するカーネルスタックをTSSに設定
    // これがないとsyscallや例外時にトリプルフォルトが発生する
    uint64_t kernel_stack_top =
        reinterpret_cast<uint64_t>(current->kernel_stack) +
        current->kernel_stack_size;
    SetKernelStack(kernel_stack_top);

    // 重要: syscall用カーネルスタックを現在のタスク専用のスタックに設定
    // これがないと、複数タスクがsyscallを呼び出した際にスタック内容が上書きされ、
    // コンテキスト切替時にクラッシュする
    g_syscall_context->kernel_stack_ptr = kernel_stack_top;

    // 重要: ユーザーモードに入る前に、GS_BASE関連のMSRを正しく設定する。
    // EnterUserModeでswapgsを使用しないため、ユーザーモードでの状態を直接設定する：
    //   - GS_BASE = 0（ユーザーモード）
    //   - KERNEL_GS_BASE = g_syscall_context（syscall時にswapgsで切り替わる）
    WriteMSR(kMSR_GS_BASE, 0);
    WriteMSR(kMSR_KERNEL_GS_BASE,
             reinterpret_cast<uint64_t>(g_syscall_context));

    // Ring 3へ遷移（この関数から戻ってこない）
    EnterUserMode(current->entry_point, sp, current->argc, argv_ptr);

    // ここには戻ってこない
    while (1)
    {
        __asm__ volatile("hlt");
    }
}
