#pragma once
#include "./../../uefi/include/efi/base.h"

typedef struct
{
    UINT64 base;  // 物理先頭 (4KiB アライン)
    UINT64 pages; // 4KiB ページ数
} PhysRange;

typedef struct
{
    UINT64 magic; /* "SLPHUEFI" などの目印 */
    UINT64 fb_base;
    UINT32 fb_size;
    UINT32 width, height, pitch, pixel_format;

    UINT64 mmap_ptr;          /* 物理アドレス(=恒等マップ前提でそのままVAとして使う) */
    UINT64 mmap_size;         /* バイト数 */
    UINT32 mmap_desc_size;    /* EFI_MEMORY_DESCRIPTOR のサイズ */
    UINT32 mmap_desc_version; /* バージョン */

    UINT64 kernel_ranges_ptr; // PhysRange* （EfiLoaderData で確保）
    UINT32 kernel_ranges_cnt; // 要素数
    UINT32 _pad_kr_;          // アライン用
} BootInfo;
