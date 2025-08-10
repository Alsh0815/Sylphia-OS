#pragma once
#include "./../../uefi/include/efi/base.h"

typedef struct
{
    UINT64 magic; /* "SLPHUEFI" 等の目印 */
    /* GOP framebuffer info */
    UINT64 fb_base; /* 物理（同時に仮想=恒等マップ前提） */
    UINT32 fb_size; /* バイト数 */
    UINT32 width;
    UINT32 height;
    UINT32 pitch;        /* PixelsPerScanLine */
    UINT32 pixel_format; /* EFI_GRAPHICS_PIXEL_FORMAT と同値 */
} BootInfo;
