#include <stddef.h>
#include "memory/memory_manager.hpp"

// 通常の new
void *operator new(size_t size)
{
    return MemoryManager::Allocate(size);
}

// 配列用 new[]
void *operator new[](size_t size)
{
    return MemoryManager::Allocate(size);
}

// 通常の delete (サイズ指定なし)
void operator delete(void *ptr) noexcept
{
    MemoryManager::Free(ptr);
}

// サイズ指定付き delete (C++14以降で必要になることがある)
void operator delete(void *ptr, size_t size) noexcept
{
    MemoryManager::Free(ptr);
}

// 配列用 delete[]
void operator delete[](void *ptr) noexcept
{
    MemoryManager::Free(ptr);
}

void operator delete[](void *ptr, size_t size) noexcept
{
    MemoryManager::Free(ptr);
}