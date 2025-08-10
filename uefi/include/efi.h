// uefi_app/include/efi.h
#pragma once
#include <stdint.h>
#include <stddef.h>

#ifndef EFIAPI
#define EFIAPI __attribute__((ms_abi))
#endif

typedef uint64_t UINTN;
typedef int64_t INTN;
typedef uint64_t UINT64;
typedef int64_t INT64;
typedef uint32_t UINT32;
typedef uint16_t UINT16;
typedef int16_t INT16;
typedef uint8_t UINT8;
typedef wchar_t CHAR16; // -fshort-wchar で 2byte にする
typedef void *EFI_HANDLE;
typedef void *EFI_EVENT;
typedef UINT64 EFI_STATUS;
typedef void VOID;
typedef UINTN EFI_TPL;
typedef UINT64 EFI_PHYSICAL_ADDRESS;

#define EFI_SUCCESS ((EFI_STATUS)0)
#define EFIERR(a) (0x8000000000000000ULL | (a))
#define EFI_LOAD_ERROR EFIERR(1)
#define EFI_INVALID_PARAMETER EFIERR(2)
#define EFI_UNSUPPORTED EFIERR(3)
#define EFI_BAD_BUFFER_SIZE EFIERR(4)
#define EFI_BUFFER_TOO_SMALL EFIERR(5)
#define EFI_NOT_READY EFIERR(6)
#define EFI_DEVICE_ERROR EFIERR(7)
#define EFI_NOT_FOUND EFIERR(14)
#define EFI_OUT_OF_RESOURCES EFIERR(9)
#define EFI_ERROR(s) ((INT64)(s) < 0)

typedef struct
{
    UINT32 Data1;
    UINT16 Data2;
    UINT16 Data3;
    UINT8 Data4[8];
} EFI_GUID;

typedef struct
{
    UINT64 Signature;
    UINT32 Revision;
    UINT32 HeaderSize;
    UINT32 CRC32;
    UINT32 Reserved;
} EFI_TABLE_HEADER;

// --- Add: EFI_TIME (UEFI Spec 2.x 準拠の最小構造体) ---
typedef struct
{
    UINT16 Year;  // 1900–9999
    UINT8 Month;  // 1–12
    UINT8 Day;    // 1–31
    UINT8 Hour;   // 0–23
    UINT8 Minute; // 0–59
    UINT8 Second; // 0–59
    UINT8 Pad1;
    UINT32 Nanosecond; // 0–999,999,999
    INT16 TimeZone;    // -1440 to 1440, or 2047
    UINT8 Daylight;    // bit0: DST, bit1: Std/Daylight
    UINT8 Pad2;
} EFI_TIME;

/*** Console I/O ***/
typedef struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
typedef struct _EFI_SIMPLE_TEXT_INPUT_PROTOCOL EFI_SIMPLE_TEXT_INPUT_PROTOCOL;

typedef EFI_STATUS(EFIAPI *EFI_TEXT_STRING)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, CHAR16 *);
typedef EFI_STATUS(EFIAPI *EFI_TEXT_CLEAR_SCREEN)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *);

struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL
{
    void *_buf1;
    EFI_TEXT_STRING OutputString;
    void *_buf2[4];
    EFI_TEXT_CLEAR_SCREEN ClearScreen;
};

typedef struct
{
    UINT16 ScanCode;
    CHAR16 UnicodeChar;
} EFI_INPUT_KEY;
typedef EFI_STATUS(EFIAPI *EFI_INPUT_READ_KEY)(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *, EFI_INPUT_KEY *);

struct _EFI_SIMPLE_TEXT_INPUT_PROTOCOL
{
    EFI_INPUT_READ_KEY ReadKeyStroke;
    void *WaitForKey;
};

/*** Boot Services ***/
typedef enum
{
    AllHandles = 0,
    ByRegisterNotify = 1,
    ByProtocol = 2
} EFI_LOCATE_SEARCH_TYPE;

typedef struct _EFI_BOOT_SERVICES EFI_BOOT_SERVICES;

typedef EFI_STATUS(EFIAPI *EFI_LOCATE_HANDLE_BUFFER)(
    EFI_LOCATE_SEARCH_TYPE, EFI_GUID *, VOID *, UINTN *, EFI_HANDLE **);

typedef EFI_STATUS(EFIAPI *EFI_HANDLE_PROTOCOL)(
    EFI_HANDLE, EFI_GUID *, VOID **);

typedef enum
{
    EfiReservedMemoryType,
    EfiLoaderCode,
    EfiLoaderData,
    EfiBootServicesCode,
    EfiBootServicesData,
    EfiRuntimeServicesCode,
    EfiRuntimeServicesData,
    EfiConventionalMemory,
    EfiUnusableMemory,
    EfiACPIReclaimMemory,
    EfiACPIMemoryNVS,
    EfiMemoryMappedIO,
    EfiMemoryMappedIOPortSpace,
    EfiPalCode,
    EfiPersistentMemory,
    EfiMaxMemoryType
} EFI_MEMORY_TYPE;

typedef EFI_STATUS(EFIAPI *EFI_ALLOCATE_POOL)(EFI_MEMORY_TYPE, UINTN, VOID **);
typedef EFI_STATUS(EFIAPI *EFI_FREE_POOL)(VOID *);

struct _EFI_BOOT_SERVICES
{
    EFI_TABLE_HEADER Hdr;

    // Task Priority Services
    void *RaiseTPL;
    void *RestoreTPL;

    // Memory Services
    void *AllocatePages;
    void *FreePages;
    void *GetMemoryMap;
    EFI_ALLOCATE_POOL AllocatePool;
    EFI_FREE_POOL FreePool;

    // Event & Timer Services
    void *CreateEvent;
    void *SetTimer;
    void *WaitForEvent;
    void *SignalEvent;
    void *CloseEvent;
    void *CheckEvent;

    // Protocol Handler Services
    void *InstallProtocolInterface;
    void *ReinstallProtocolInterface;
    void *UninstallProtocolInterface;
    EFI_HANDLE_PROTOCOL HandleProtocol;
    void *Reserved;
    void *RegisterProtocolNotify;
    void *LocateHandle;
    void *LocateDevicePath;
    void *InstallConfigurationTable;

    // Image Services
    void *LoadImage;
    void *StartImage;
    void *Exit;
    void *UnloadImage;
    void *ExitBootServices;

    // Miscellaneous Services
    void *GetNextMonotonicCount;
    void *Stall;
    void *SetWatchdogTimer;

    // Driver Support Services
    void *ConnectController;
    void *DisconnectController;

    // Open and Close Protocol Services
    void *OpenProtocol;
    void *CloseProtocol;
    void *OpenProtocolInformation;

    // Library Services
    void *ProtocolsPerHandle;
    EFI_LOCATE_HANDLE_BUFFER LocateHandleBuffer;
    void *LocateProtocol;
    void *InstallMultipleProtocolInterfaces;
    void *UninstallMultipleProtocolInterfaces;

    // 32-bit CRC Services
    void *CalculateCrc32;

    // Miscellaneous Services
    void *CopyMem;
    void *SetMem;
    void *CreateEventEx;
};

/*** System Table ***/
typedef struct
{
    EFI_TABLE_HEADER Hdr;
    CHAR16 *FirmwareVendor;
    UINT32 FirmwareRevision;
    EFI_HANDLE ConsoleInHandle;
    EFI_SIMPLE_TEXT_INPUT_PROTOCOL *ConIn;
    EFI_HANDLE ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
    EFI_HANDLE StandardErrorHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *StdErr;
    void *RuntimeServices;
    EFI_BOOT_SERVICES *BootServices;
    UINTN NumberOfTableEntries;
    void *ConfigurationTable;
} EFI_SYSTEM_TABLE;

/*** お気軽ユーティリティ ***/
static inline UINTN StrLen16(const CHAR16 *s)
{
    UINTN n = 0;
    while (s && s[n])
        n++;
    return n;
}
static inline void *MemSet(void *p, int c, UINTN n)
{
    UINT8 *b = (UINT8 *)p;
    for (UINTN i = 0; i < n; i++)
        b[i] = (UINT8)c;
    return p;
}
static inline void *MemCpy(void *d, const void *s, UINTN n)
{
    UINT8 *dd = (UINT8 *)d;
    const UINT8 *ss = (const UINT8 *)s;
    for (UINTN i = 0; i < n; i++)
        dd[i] = ss[i];
    return d;
}
