#include "app_wrapper.hpp"
#include "elf_loader.hpp"
#include "printk.hpp"
#include "task/scheduler.hpp"
#include "task/task_manager.hpp"
#include <std/string.hpp>

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

    kprintf("[AppTask] Starting '%s' (ID=%lu)\n", current->name,
            current->task_id);

    // ELFアプリのエントリーポイントを関数ポインタに変換して呼び出し
    typedef int (*AppMain)(int argc, char **argv);
    AppMain app_main = reinterpret_cast<AppMain>(current->entry_point);

    // アプリを実行
    int result = app_main(current->argc, current->argv);

    kprintf("[AppTask] '%s' exited with code %d\n", current->name, result);

    // タスクを終了
    Task *task_to_terminate = current;
    TaskManager::SetCurrentTask(nullptr);
    TaskManager::TerminateTask(task_to_terminate);

    // 次のタスクへ切り替え
    Scheduler::Schedule();

    // ここには戻ってこない
    while (1)
    {
        __asm__ volatile("hlt");
    }
}
