#include "scheduler.hpp"

void Scheduler::AddTask(Task *task)
{
    ready_queue_.push_back(task);
}

// 最初のタスクを開始する
void Scheduler::Start()
{
    if (ready_queue_.empty())
    {
        return;
    }

    running_task_ = ready_queue_.front();
    ready_queue_.erase(ready_queue_.begin());
    running_task_->SetState(TaskState::RUNNING);

    // 最初のタスクのコンテキストをロードして実行開始
    // current_ctxは使われないのでnullptrを渡す
    switch_context(running_task_->GetContext(), nullptr);
}

void Scheduler::Schedule()
{
    // コンテキスト保存処理はここで行わない
    if (ready_queue_.empty())
    {
        return;
    }

    // 現在のタスクを実行待ちキューの末尾に戻す
    running_task_->SetState(TaskState::READY);
    ready_queue_.push_back(running_task_);

    // キューの先頭から次のタスクを取り出し、running_task_を更新する
    running_task_ = ready_queue_.front();
    ready_queue_.erase(ready_queue_.begin());
    running_task_->SetState(TaskState::RUNNING);
}

extern "C" void switch_context(Context *next_ctx, Context *current_ctx)
{
    asm volatile(
        // === 現在のコンテキストを保存 (current_ctxがnullptrの場合はスキップ) ===
        "test %1, %1 \n\t"
        "jz 1f \n\t"
        "mov %%r15, 8(%1) \n\t"  // オフセット 16 -> 8
        "mov %%r14, 16(%1) \n\t" // オフセット 24 -> 16
        "mov %%r13, 24(%1) \n\t" // オフセット 32 -> 24
        "mov %%r12, 32(%1) \n\t" // オフセット 40 -> 32
        "mov %%rbp, 40(%1) \n\t" // オフセット 48 -> 40
        "mov %%rbx, 48(%1) \n\t" // オフセット 56 -> 48
        "1: \n\t"                // ラベル
        // === 次のコンテキストを復元 ===
        "mov 8(%0), %%r15 \n\t"  // オフセット 16 -> 8
        "mov 16(%0), %%r14 \n\t" // オフセット 24 -> 16
        "mov 24(%0), %%r13 \n\t" // オフセット 32 -> 24
        "mov 32(%0), %%r12 \n\t" // オフセット 40 -> 32
        "mov 40(%0), %%rbp \n\t" // オフセット 48 -> 40
        "mov 48(%0), %%rbx \n\t" // オフセット 56 -> 48
        "mov 72(%0), %%rsp \n\t" // スタックポインタを切り替え (オフセット 80 -> 72)
        "mov 64(%0), %%rax \n\t" // rflagsを復元 (オフセット 72 -> 64)
        "push %%rax \n\t"
        "popf \n\t"
        "mov 56(%0), %%rax \n\t" // ripをスタックに積む (オフセット 64 -> 56)
        "push %%rax \n\t"
        // cr3レジスタを復元
        "mov 0(%0), %%rax \n\t"
        "mov %%rax, %%cr3 \n\t"
        "ret \n\t" // retでスタックに積んだripにジャンプ！
        :
        : "D"(next_ctx), "S"(current_ctx)
        : "memory", "rax", "rbx", "rbp", "r12", "r13", "r14", "r15");
}