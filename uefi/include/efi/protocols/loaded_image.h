// uefi/include/efi/protocols/loaded_image.h
#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#include "../base.h"         // EFI_HANDLE, EFI_STATUS, UINT32/64 など
#include "../system_table.h" // EFI_SYSTEM_TABLE

    // 前方宣言（中身は不要）
    typedef struct EFI_DEVICE_PATH_PROTOCOL EFI_DEVICE_PATH_PROTOCOL;

    // {5B1B31A1-9562-11d2-8E3F-00A0C969723B}
    static const EFI_GUID EFI_LOADED_IMAGE_PROTOCOL_GUID = {0x5B1B31A1, 0x9562, 0x11d2, {0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}};

    // UEFI Spec 定義の最小版
    typedef struct EFI_LOADED_IMAGE_PROTOCOL
    {
        UINT32 Revision;
        EFI_HANDLE ParentHandle;
        EFI_SYSTEM_TABLE *SystemTable;

        // ★今回必要なのはこれ（このデバイスの SimpleFS を開く）
        EFI_HANDLE DeviceHandle;

        EFI_DEVICE_PATH_PROTOCOL *FilePath;
        VOID *Reserved;

        UINT32 LoadOptionsSize;
        VOID *LoadOptions;

        VOID *ImageBase;
        UINT64 ImageSize;
        EFI_MEMORY_TYPE ImageCodeType;
        EFI_MEMORY_TYPE ImageDataType;

        EFI_STATUS(EFIAPI *Unload)(EFI_HANDLE ImageHandle);
    } EFI_LOADED_IMAGE_PROTOCOL;

#ifdef __cplusplus
}
#endif
