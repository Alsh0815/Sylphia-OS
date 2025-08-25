#pragma once
#include "../pmm_vector.hpp"
#include "task.hpp"

class Scheduler
{
public:
    static Scheduler &GetInstance()
    {
        static Scheduler instance;
        return instance;
    }

    void AddTask(Task *task);
    Task *GetRunningTask() const { return running_task_; }
    void Start(); // 最初のタスクを実行開始

    void Schedule();

private:
    Scheduler() = default;
    ~Scheduler() = default;

    PmmVec<Task *> ready_queue_;   // 実行待ちタスクのキュー
    Task *running_task_ = nullptr; // 現在実行中のタスク
    size_t current_task_index_ = 0;
};

// コンテキストスイッチを行うアセンブリ関数（外部で実装）
extern "C" void switch_context(Context *next_ctx, Context *current_ctx);