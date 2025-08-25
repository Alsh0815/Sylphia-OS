#pragma once

#include <cstdint>
#include "context.hpp"

enum class TaskState
{
    RUNNING, // 現在実行中
    READY,   // 実行可能（待機中）
    BLOCKED, // 何らかのイベントを待っている（今回は未使用）
};

using TaskId = uint64_t;

class Task
{
public:
    Task(TaskId id, uint64_t entry_point);
    ~Task();

    TaskId GetId() const { return id_; }
    TaskState GetState() const { return state_; }
    Context *GetContext() { return context_; }

    void SetState(TaskState state) { state_ = state; }

private:
    TaskId id_;
    TaskState state_;

    uint64_t *kernel_stack_; // このタスク用のカーネルスタック
    Context *context_;       // スタックの最上部に配置されるコンテキスト
};