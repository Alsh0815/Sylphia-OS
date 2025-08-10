// uefi_app/include/efiprot.h
#pragma once
#include "efi.h"

/*** Simple File System Protocol ***/

typedef struct
{
    UINT64 Size;
    UINT64 FileSize;
    UINT64 PhysicalSize;
    EFI_TIME /*opaque*/ *CreateTime;
    EFI_TIME /*opaque*/ *LastAccessTime;
    EFI_TIME /*opaque*/ *ModificationTime;
    UINT64 Attribute;
    CHAR16 FileName[1];
} EFI_FILE_INFO;

typedef struct _EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;

typedef EFI_STATUS(EFIAPI *EFI_FILE_OPEN)(
    EFI_FILE_PROTOCOL *, EFI_FILE_PROTOCOL **, CHAR16 *, UINT64, UINT64);
typedef EFI_STATUS(EFIAPI *EFI_FILE_CLOSE)(EFI_FILE_PROTOCOL *);
typedef EFI_STATUS(EFIAPI *EFI_FILE_DELETE)(EFI_FILE_PROTOCOL *);
typedef EFI_STATUS(EFIAPI *EFI_FILE_READ)(EFI_FILE_PROTOCOL *, UINTN *, VOID *);
typedef EFI_STATUS(EFIAPI *EFI_FILE_WRITE)(EFI_FILE_PROTOCOL *, UINTN *, const VOID *);
typedef EFI_STATUS(EFIAPI *EFI_FILE_GET_POSITION)(EFI_FILE_PROTOCOL *, UINT64 *);
typedef EFI_STATUS(EFIAPI *EFI_FILE_SET_POSITION)(EFI_FILE_PROTOCOL *, UINT64);
typedef EFI_STATUS(EFIAPI *EFI_FILE_GET_INFO)(EFI_FILE_PROTOCOL *, EFI_GUID *, UINTN *, VOID *);
typedef EFI_STATUS(EFIAPI *EFI_FILE_SET_INFO)(EFI_FILE_PROTOCOL *, EFI_GUID *, UINTN, VOID *);
typedef EFI_STATUS(EFIAPI *EFI_FILE_FLUSH)(EFI_FILE_PROTOCOL *);

struct _EFI_FILE_PROTOCOL
{
    UINT64 Revision;
    EFI_FILE_OPEN Open;
    EFI_FILE_CLOSE Close;
    EFI_FILE_DELETE Delete;
    EFI_FILE_READ Read;
    EFI_FILE_WRITE Write;
    EFI_FILE_GET_POSITION GetPosition;
    EFI_FILE_SET_POSITION SetPosition;
    EFI_FILE_GET_INFO GetInfo;
    EFI_FILE_SET_INFO SetInfo;
    EFI_FILE_FLUSH Flush;

    /* UEFI 2.x 以降の拡張スロット（存在しても順序に影響しないようダミー） */
    void *OpenEx;
    void *ReadEx;
    void *WriteEx;
    void *FlushEx;
};

typedef struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;
typedef EFI_STATUS (*EFI_OPEN_VOLUME)(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *, EFI_FILE_PROTOCOL **);

struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL
{
    UINT64 Revision;
    EFI_OPEN_VOLUME OpenVolume;
};

/*** GUIDs ***/
static const EFI_GUID EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID =
    {0x964e5b22, 0x6459, 0x11d2, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};
static const EFI_GUID EFI_FILE_INFO_ID =
    {0x09576e92, 0x6d3f, 0x11d2, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};

/*** File open modes & attributes ***/
#define EFI_FILE_MODE_READ 0x0000000000000001ULL
#define EFI_FILE_MODE_WRITE 0x0000000000000002ULL
#define EFI_FILE_MODE_CREATE 0x8000000000000000ULL
