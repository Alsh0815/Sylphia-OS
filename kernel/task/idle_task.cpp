#include "idle_task.hpp"
#include "app/elf/elf_loader.hpp"
#include "arch/inasm.hpp"
#include "io.hpp"
#include "printk.hpp"
#include "sys/logger/logger.hpp"
#include "sys/std/file_descriptor.hpp"
#include "sys/sys.hpp"
#include "task_manager.hpp"

Task *g_idle_task = nullptr;

// 必須プロセスの管理
namespace EssentialProcesses
{
// 必須プロセス情報
struct EssentialProcess
{
    const char *path; // ELFパス
    bool started;     // 起動済みかどうか
    uint64_t task_id; // 起動したタスクID（0=未起動）
};

// 必須プロセスリスト
static EssentialProcess processes[] = {
    {"/sys/bin/shell.elf", false, 0},
};

static const int kProcessCount = sizeof(processes) / sizeof(processes[0]);
static bool initialized = false;

// 必須プロセスをチェックして起動
void CheckAndStartProcesses()
{
    for (int i = 0; i < kProcessCount; ++i)
    {
        if (!processes[i].started)
        {
            // プロセス起動を試みる
            char *argv[] = {(char *)processes[i].path, nullptr};
            Task *task = ElfLoader::CreateProcess(processes[i].path, 1, argv);

            if (task)
            {
                processes[i].started = true;
                processes[i].task_id = task->task_id;
                kprintf("[EssentialProc] Started: %s (ID=%lu)\n",
                        processes[i].path, task->task_id);
                Sys::Logger::g_event_logger->Info(
                    Sys::Logger::LogType::Kernel,
                    "Essential process started: shell");
            }
            else
            {
                // 起動失敗（ファイルが存在しないなど）
                // 毎回試行すると負荷になるため、一度だけ警告
                static bool warned[16] = {false};
                if (!warned[i])
                {
                    kprintf("[EssentialProc] WARNING: Failed to start %s\n",
                            processes[i].path);
                    warned[i] = true;
                }
            }
        }
    }
}

// 必須プロセス管理を初期化
void Initialize()
{
    if (!initialized)
    {
        initialized = true;
        kprintf("[EssentialProc] Initialized with %d processes.\n",
                kProcessCount);
    }
}

} // namespace EssentialProcesses

void IdleTaskEntry()
{
    kprintf("[IdleTask] *** TASK STARTED! ***\n");

    // 必須プロセス管理初期化
    EssentialProcesses::Initialize();

    // アイドルタスク: hltで待機し、タイマー割り込みで起こされる
    while (1)
    {
        // 必須プロセスのチェックと起動
        EssentialProcesses::CheckAndStartProcesses();

        Hlt(); // CPUを停止して割り込み待ち
    }
}

void InitializeIdleTask()
{
    // アイドルタスクを作成
    g_idle_task =
        TaskManager::CreateTask(reinterpret_cast<uint64_t>(IdleTaskEntry));

    if (g_idle_task)
    {
        TaskManager::AddToReadyQueue(g_idle_task);
        kprintf("[IdleTask] Created and added to ready queue.\n");
    }
    else
    {
        kprintf("[IdleTask] Failed to create idle task!\n");
    }
}
