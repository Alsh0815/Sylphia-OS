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

class alignas(16) Task
{
public:
    void *operator new(size_t size);
    void operator delete(void *ptr);

    Task(TaskId id, uint64_t entry_point);
    ~Task();

    TaskId GetId() const { return id_; }
    TaskState GetState() const { return state_; }
    Context *GetContext() { return &context_; }
    bool GetFirstFlag() { return first_; }

    void SetFirstFlagFalse() { first_ = false; }
    void SetState(TaskState state) { state_ = state; }

private:
    TaskId id_;
    TaskState state_;
    bool first_ = true;
    
    uint8_t *raw_stack_buffer_;
    Context context_;
};