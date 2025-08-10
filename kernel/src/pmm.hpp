#pragma once
#include <stdint.h>
#include "bootinfo.h"

namespace pmm
{
    uint64_t init(const BootInfo &bi);

    // 単位は「4KiBページ」
    void *alloc_pages(uint64_t npages = 1); // 物理アドレスを返す (void*として)
    void free_pages(void *phys, uint64_t npages = 1);

    uint64_t total_bytes(); // 管理総量
    uint64_t free_bytes();  // 空き総量
    uint64_t used_bytes();  // 使用総量 (=total-free)

    void reserve_range(uint64_t phys_base, uint64_t pages);

}
