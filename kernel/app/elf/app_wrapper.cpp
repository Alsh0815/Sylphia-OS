#include "app_wrapper.hpp"
#include "elf_loader.hpp"
#include "paging.hpp"
#include "printk.hpp"
#include "task/scheduler.hpp"
#include "task/task_manager.hpp"
#include <std/string.hpp>

// asmfunc.asmで定義
extern "C" void EnterUserMode(uint64_t entry_point, uint64_t user_stack_top,
                              int argc, uint64_t argv_ptr);

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

    // アプリ実行中フラグを設定（シェルへのキー入力を抑制）
    g_app_running = true;

    // Ring 3へ遷移（この関数から戻ってこない）
    EnterUserMode(current->entry_point, sp, current->argc, argv_ptr);

    // ここには戻ってこない
    while (1)
    {
        __asm__ volatile("hlt");
    }
}
