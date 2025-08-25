#include "task.hpp"
#include <cstring> // for memset

namespace
{
    // 各タスクに割り当てるカーネルスタックのサイズ (8KB)
    constexpr size_t kKernelStackSize = 8 * 1024;
}

Task::Task(TaskId id, uint64_t entry_point)
    : id_(id), state_(TaskState::READY)
{
    // 1. カーネルスタック領域を確保
    const size_t stack_size_in_qword = kKernelStackSize / sizeof(uint64_t);
    kernel_stack_ = new uint64_t[stack_size_in_qword];
    // 2. スタックの最上部にContext構造体を配置
    context_ = reinterpret_cast<Context *>(&kernel_stack_[stack_size_in_qword] - sizeof(Context) / sizeof(uint64_t));
    // 3. コンテキストの初期化
    memset(context_, 0, sizeof(Context));
    // タスクの実行開始アドレスを設定
    context_->rip = entry_point;
    // RFLAGS: 割り込みを有効にする (IF=1)
    context_->rflags = 0x202;

    // CR3: ページテーブルベースレジスタ (カーネルのものを使う)
    // RSP: スタックポインタ (コンテキスト構造体の直下を指す)
    asm volatile("mov %%cr3, %0" : "=r"(context_->cr3));
    context_->rsp = reinterpret_cast<uint64_t>(context_);
}

Task::~Task()
{
    delete[] kernel_stack_;
}