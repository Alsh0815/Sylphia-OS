#include "memory/memory_manager.hpp"
#include "graphics.hpp"

// staticメンバ変数の実体定義
uintptr_t MemoryManager::heap_start_ = 0;
uintptr_t MemoryManager::heap_end_ = 0;
uintptr_t MemoryManager::current_pos_ = 0;

void MemoryManager::Initialize(const MemoryMap &memmap)
{
    uintptr_t max_free_size = 0;
    uintptr_t max_free_start = 0;

    // メモリマップを走査して、一番大きい ConventionalMemory を探す
    uintptr_t iter = reinterpret_cast<uintptr_t>(memmap.buffer);
    for (unsigned int i = 0; i < memmap.map_size / memmap.descriptor_size; ++i)
    {
        auto *desc = reinterpret_cast<const MemoryDescriptor *>(iter);

        if (static_cast<MemoryType>(desc->type) == MemoryType::kEfiConventionalMemory)
        {
            uintptr_t size = desc->number_of_pages * 4096;
            // 今まで見つけた中で一番デカいなら記録更新
            if (size > max_free_size)
            {
                max_free_size = size;
                max_free_start = desc->physical_start;
            }
        }
        iter += memmap.descriptor_size;
    }

    // ヒープとして設定
    heap_start_ = max_free_start;
    heap_end_ = max_free_start + max_free_size;
    current_pos_ = heap_start_;
}

void *MemoryManager::Allocate(size_t size)
{
    // 16バイト境界にアライメントする (CPUの最適化のため)
    const int alignment = 16;
    current_pos_ = (current_pos_ + alignment - 1) & ~(alignment - 1);

    if (current_pos_ + size > heap_end_)
    {
        return nullptr; // Out of Memory
    }

    void *ptr = reinterpret_cast<void *>(current_pos_);
    current_pos_ += size;
    return ptr;
}

void MemoryManager::Free(void *ptr)
{
    // バンプアロケータは「積み上げ式」なので個別に解放不可
    // 解放機能（虫食い状のメモリ管理）は、将来「フリーリスト」などを実装する時に追加
}