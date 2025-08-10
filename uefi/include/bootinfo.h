#pragma once
#include "./efi/base.h"

typedef struct
{
    UINT64 magic; /* 例: 0x534C5048ULL ('SLPH') など */
} BootInfo;
