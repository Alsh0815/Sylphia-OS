#pragma once
#include "./efi/base.h"

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
} BootInfo;
