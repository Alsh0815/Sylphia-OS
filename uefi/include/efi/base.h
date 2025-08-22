#pragma once
#include <stdint.h>
#include <stddef.h>

/* 呼び出し規約 */
#define EFIAPI __attribute__((ms_abi))

/* 基本型 */
typedef uint64_t UINTN;
typedef int64_t INTN;
typedef uint64_t UINT64;
typedef int64_t INT64;
typedef uint32_t UINT32;
typedef int32_t INT32;
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

/* ステータス */
#define EFI_SUCCESS ((EFI_STATUS)0)
#define EFIERR(a) (0x8000000000000000ULL | (a))
#define EFI_LOAD_ERROR EFIERR(1)
#define EFI_INVALID_PARAMETER EFIERR(2)
#define EFI_UNSUPPORTED EFIERR(3)
#define EFI_BAD_BUFFER_SIZE EFIERR(4)
#define EFI_BUFFER_TOO_SMALL EFIERR(5)
#define EFI_NOT_READY EFIERR(6)
#define EFI_DEVICE_ERROR EFIERR(7)
#define EFI_OUT_OF_RESOURCES EFIERR(9)
#define EFI_NOT_FOUND EFIERR(14)
#define EFI_ERROR(s) ((INT64)(s) < 0)

/* GUID / Table header */
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

/* EFI_TIME */
typedef struct
{
    UINT16 Year;
    UINT8 Month;
    UINT8 Day;
    UINT8 Hour;
    UINT8 Minute;
    UINT8 Second;
    UINT8 Pad1;
    UINT32 Nanosecond;
    INT16 TimeZone;
    UINT8 Daylight;
    UINT8 Pad2;
} EFI_TIME;

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

/* 小ユーティリティ */
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
