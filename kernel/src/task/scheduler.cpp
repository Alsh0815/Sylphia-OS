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

    Context *context_ = running_task_->GetContext();
    switch_context(context_, nullptr);
}

void Scheduler::Schedule()
{
    if (ready_queue_.empty())
    {
        return;
    }

    // 現在のタスクを実行待ちキューの末尾に戻す
    running_task_->SetState(TaskState::READY);
    running_task_->SetFirstFlagFalse();
    ready_queue_.push_back(running_task_);

    // キューの先頭から次のタスクを取り出し、running_task_を更新する
    running_task_ = ready_queue_.front();
    ready_queue_.erase(ready_queue_.begin());
    running_task_->SetState(TaskState::RUNNING);
}

extern "C" void switch_context(Context *next_ctx, Context *current_ctx)
{
    asm volatile(
        "test %1, %1 \n\t" // If current_ctx == nullptr
        "jz 1f \n\t"       // Jump to label 1
        "fxsave 192(%1) \n\t"
        "mov %%r15, 184(%1) \n\t" // R15 -> Context(184)
        "mov %%r14, 176(%1) \n\t" // R14 -> Context(176)
        "mov %%r13, 168(%1) \n\t" // R13 -> Context(168)
        "mov %%r12, 160(%1) \n\t" // R12 -> Context(160)
        "mov %%r11, 152(%1) \n\t" // R11 -> Context(152)
        "mov %%r10, 144(%1) \n\t" // R10 -> Context(144)
        "mov %%r9, 136(%1) \n\t"  // R9 -> Context(136)
        "mov %%r8, 128(%1) \n\t"  // R8 -> Context(128)
        "mov %%rbp, 120(%1) \n\t" // RBP -> Context(120)
        "mov %%rsi, 104(%1) \n\t" // RSI -> Context(104)
        "mov %%rdi, 96(%1) \n\t"  // RDI -> Context(96)
        "mov %%rdx, 88(%1) \n\t"  // RDX -> Context(88)
        "mov %%rcx, 80(%1) \n\t"  // RCX -> Context(80)
        "mov %%rbx, 72(%1) \n\t"  // RBX -> Context(72)
        "mov %%rax, 64(%1) \n\t"  // RAX -> Context(64)

        //"lea 8(%%rsp), %%rax \n\t"
        //"mov %%rax, 112(%1) \n\t" // RAX -> Context(112)

        "mov %%cr3, %%rax \n\t" // CR3 -> RAX
        "mov %%rax, 0(%1) \n\t" // RAX -> Context(0)
        //"mov (%%rsp), %%rax \n\t" // RSP ADDR (RIP) -> RAX
        //"mov %%rax, 8(%1) \n\t"   // RAX -> Context(8)
        //"pushfq \n\t"             // RFLAGS -> Stack
        //"popq 16(%1) \n\t"        // Stack -> Context(16)

        //"mov %%cs, %%ax \n\t"    // CS -> AX
        //"mov %%rax, 32(%1) \n\t" // RAX -> Context(32)
        //"mov %%ss, %%bx \n\t"    // SS -> BX
        //"mov %%rbx, 40(%1) \n\t" // RBX -> Context(40)
        "mov %%fs, %%cx \n\t"    // FS -> CX
        "mov %%rcx, 48(%1) \n\t" // RCX -> Context(48)
        "mov %%gs, %%dx \n\t"    // GS -> DX
        "mov %%rdx, 56(%1) \n\t" // RDX -> Context(56)

        "1: \n\t"
        "pushq 40(%0) \n\t"  // SS
        "pushq 112(%0) \n\t" // RSP
        "pushq 16(%0) \n\t"  // RFLAGS
        "pushq 32(%0) \n\t"  // CS
        "pushq 8(%0) \n\t"   // RIP

        "fxrstor 192(%0) \n\t"

        "mov 0(%0), %%rax \n\t"  // Context(0) -> RAX
        "mov %%rax, %%cr3 \n\t"  // RAX -> CR3
        "mov 48(%0), %%rax \n\t" // Context(48) -> RAX
        "mov %%ax, %%fs \n\t"    // AX -> FS
        "mov 56(%0), %%rax \n\t" // Context(56) -> RAX
        "mov %%ax, %%gs \n\t"    // AX -> GS

        "mov 184(%0), %%r15 \n\t"
        "mov 176(%0), %%r14 \n\t"
        "mov 168(%0), %%r13 \n\t"
        "mov 160(%0), %%r12 \n\t"
        "mov 152(%0), %%r11 \n\t"
        "mov 144(%0), %%r10 \n\t"
        "mov 136(%0), %%r9 \n\t"
        "mov 128(%0), %%r8 \n\t"
        "mov 120(%0), %%rbp \n\t"
        "mov 104(%0), %%rsi \n\t"
        "mov 88(%0), %%rdx \n\t"
        "mov 80(%0), %%rcx \n\t"
        "mov 72(%0), %%rbx \n\t"
        "mov 64(%0), %%rax \n\t"

        "mov 96(%0), %%rdi \n\t" // Context(96) -> RDI

        "iretq \n\t"
        :
        : "D"(next_ctx), "S"(current_ctx),
          [off_rsp] "i"(offsetof(Context, rsp)),
          [off_rip] "i"(offsetof(Context, rip))
        : "memory", "cc", "cr3", "rax", "rbx", "rcx", "rdx", "rsp", "rbp",
          "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15");
}