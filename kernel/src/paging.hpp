#pragma once
#include <stdint.h>
#include "../include/bootinfo.h"

namespace paging
{

    // 初期化: BootInfoのmemmapを使い、恒等マップ(2MiB pages)を構築してCR3切替
    // 返り値: PML4の物理アドレス（デバッグ用）。失敗時は0。
    uint64_t init_identity(const BootInfo &bi);

    // デバッグ向け: どこまでマップしたか（終端物理アドレス）
    uint64_t mapped_limit();

    bool init_allocator(const BootInfo &bi);
    void *alloc_low_stack(size_t bytes);
    void *alloc_page4k();
    bool map_mmio_at(uint64_t va, uint64_t phys, uint64_t size);
    bool map_mmio_range(uint64_t phys, uint64_t size);
    bool dbg_probe_mmio_mapped(uint64_t phys_addr);

} // namespace paging
