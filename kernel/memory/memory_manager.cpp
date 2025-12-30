#include "memory/memory_manager.hpp"
#include "arch/inasm.hpp"
#include "cxx.hpp"
#include "graphics.hpp"
#include "printk.hpp"

extern "C" char __kernel_start;
extern "C" char __kernel_end;

Bitmap MemoryManager::bitmap_;
uintptr_t MemoryManager::range_begin_ = 0;
uintptr_t MemoryManager::range_end_ = 0;

void MemoryManager::Initialize(const MemoryMap &memmap)
{
    uintptr_t iter = reinterpret_cast<uintptr_t>(memmap.buffer);
    for (unsigned int i = 0; i < memmap.map_size / memmap.descriptor_size; ++i)
    {
        auto *desc = reinterpret_cast<const MemoryDescriptor *>(iter);
        uintptr_t region_end =
            desc->physical_start + (desc->number_of_pages * 4096);
        if (region_end > range_end_)
        {
            range_end_ = region_end;
        }
        iter += memmap.descriptor_size;
    }

    size_t total_frames = range_end_ / kFrameSize;
    size_t bitmap_size = (total_frames + 7) / 8; // 切り上げ

    uintptr_t bitmap_base = 0;
    iter = reinterpret_cast<uintptr_t>(memmap.buffer);
    for (unsigned int i = 0; i < memmap.map_size / memmap.descriptor_size; ++i)
    {
        auto *desc = reinterpret_cast<const MemoryDescriptor *>(iter);
        if (static_cast<MemoryType>(desc->type) ==
            MemoryType::kEfiConventionalMemory)
        {
            size_t region_size = desc->number_of_pages * kFrameSize;
            if (region_size >= bitmap_size)
            {
                bitmap_base = desc->physical_start;
                break;
            }
        }
        iter += memmap.descriptor_size;
    }

    if (bitmap_base == 0)
    {
        kprintf("Error: No suitable memory region found for bitmap.\n");
        while (1)
            Hlt();
    }

    bitmap_.SetBuffer(reinterpret_cast<uint8_t *>(bitmap_base), bitmap_size);

    memset(reinterpret_cast<void *>(bitmap_base), 0xFF, bitmap_size);

    iter = reinterpret_cast<uintptr_t>(memmap.buffer);
    for (unsigned int i = 0; i < memmap.map_size / memmap.descriptor_size; ++i)
    {
        auto *desc = reinterpret_cast<const MemoryDescriptor *>(iter);
        if (static_cast<MemoryType>(desc->type) ==
            MemoryType::kEfiConventionalMemory)
        {
            // この領域に含まれるフレームをすべて「空き」にする
            uintptr_t start_frame = desc->physical_start / kFrameSize;
            uintptr_t end_frame =
                (desc->physical_start + desc->number_of_pages * kFrameSize) /
                kFrameSize;

            for (size_t f = start_frame; f < end_frame; ++f)
            {
                bitmap_.Set(f, false); // 0 = 空き
            }
        }
        iter += memmap.descriptor_size;
    }

    uintptr_t bitmap_start_frame = bitmap_base / kFrameSize;
    uintptr_t bitmap_end_frame =
        (bitmap_base + bitmap_size + kFrameSize - 1) / kFrameSize;
    for (size_t f = bitmap_start_frame; f < bitmap_end_frame; ++f)
    {
        bitmap_.Set(f, true); // 使用中
    }

    uintptr_t k_start = reinterpret_cast<uintptr_t>(&__kernel_start);
    uintptr_t k_end = reinterpret_cast<uintptr_t>(&__kernel_end);

    size_t k_start_frame = k_start / kFrameSize;
    size_t k_end_frame = (k_end + kFrameSize - 1) / kFrameSize;

    for (size_t f = k_start_frame; f < k_end_frame; ++f)
    {
        bitmap_.Set(f, true); // 使用中(1)にマーク
    }

    bitmap_.Set(0, true);
}

// 1フレーム(4KB)だけ確保する
void *MemoryManager::AllocateFrame()
{
    long frame = bitmap_.FindFreeFrame();
    if (frame < 0)
        return nullptr; // 空きなし

    bitmap_.Set(frame, true); // 使用中にマーク
    return reinterpret_cast<void *>(frame * kFrameSize);
}

void MemoryManager::FreeFrame(void *ptr)
{
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    size_t frame = addr / kFrameSize;
    bitmap_.Set(frame, false); // 空きに戻す
}

// 複数ページ(連続領域)の確保
// 簡易的に「連続した空きビット」を探す (First Fit)
void *MemoryManager::Allocate(size_t size, size_t alignment)
{
    if (size == 0)
        return nullptr;

    // 必要なフレーム数
    size_t num_frames = (size + kFrameSize - 1) / kFrameSize;

    // 1フレームなら高速版を使う
    if (num_frames == 1)
        return AllocateFrame();

    // 連続領域の探索
    // ※効率は悪いが、単純な線形探索を行う
    size_t total_frames = range_end_ / kFrameSize;

    // FindFreeFrameで見つけた場所からチェックを始めると少し速い
    long start_search = bitmap_.FindFreeFrame();
    if (start_search < 0)
        return nullptr;

    for (size_t i = start_search; i < total_frames; ++i)
    {
        // ここから num_frames 分空いているか確認
        bool found = true;
        for (size_t j = 0; j < num_frames; ++j)
        {
            if (i + j >= total_frames || bitmap_.Get(i + j))
            {
                found = false;
                // 次の探索は j 分飛ばせる
                i += j;
                break;
            }
        }

        if (found)
        {
            // 見つかったので確保
            for (size_t j = 0; j < num_frames; ++j)
            {
                bitmap_.Set(i + j, true);
            }
            return reinterpret_cast<void *>(i * kFrameSize);
        }
    }

    return nullptr;
}

void MemoryManager::Free(void *ptr, size_t size)
{
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    size_t start_frame = addr / kFrameSize;
    size_t num_frames = (size + kFrameSize - 1) / kFrameSize;

    for (size_t i = 0; i < num_frames; ++i)
    {
        bitmap_.Set(start_frame + i, false);
    }
}