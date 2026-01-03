#pragma once

#include "memory/memory_manager.hpp"
#include <stddef.h>

#pragma GCC diagnostic ignored "-Wnew-returns-null"
#pragma GCC diagnostic ignored "-Wnonnull"

const size_t kHeaderSize = 16;

// Placement new (配置new) - 指定されたアドレスにオブジェクトを構築
inline void *operator new(size_t, void *ptr) noexcept
{
    return ptr;
}

inline void *operator new[](size_t, void *ptr) noexcept
{
    return ptr;
}

inline void operator delete(void *, void *) noexcept {}
inline void operator delete[](void *, void *) noexcept {}

// 通常の new/delete 演算子の宣言（定義は new.cpp にあります）
void *operator new(size_t size);
void *operator new[](size_t size);
void operator delete(void *ptr) noexcept;
void operator delete(void *ptr, size_t size) noexcept;
void operator delete[](void *ptr) noexcept;
void operator delete[](void *ptr, size_t size) noexcept;