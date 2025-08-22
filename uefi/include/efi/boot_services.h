#pragma once
#include "base.h"

typedef enum
{
    AllocateAnyPages = 0,
    AllocateMaxAddress = 1,
    AllocateAddress = 2
} EFI_ALLOCATE_TYPE;

typedef struct
{
    UINT32 Type;
    UINT64 PhysicalStart;
    UINT64 VirtualStart;
    UINT64 NumberOfPages;
    UINT64 Attribute;
} EFI_MEMORY_DESCRIPTOR;

typedef EFI_STATUS(EFIAPI *EFI_ALLOCATE_PAGES)(EFI_ALLOCATE_TYPE, EFI_MEMORY_TYPE, UINTN, EFI_PHYSICAL_ADDRESS *);
typedef EFI_STATUS(EFIAPI *EFI_FREE_PAGES)(EFI_PHYSICAL_ADDRESS, UINTN);
typedef EFI_STATUS(EFIAPI *EFI_GET_MEMORY_MAP)(UINTN *, VOID *, UINTN *, UINTN *, UINT32 *);
typedef EFI_STATUS(EFIAPI *EFI_ALLOCATE_POOL)(EFI_MEMORY_TYPE, UINTN, VOID **);
typedef EFI_STATUS(EFIAPI *EFI_FREE_POOL)(VOID *);
typedef EFI_STATUS(EFIAPI *EFI_HANDLE_PROTOCOL)(EFI_HANDLE, EFI_GUID *, VOID **);
typedef enum
{
    AllHandles = 0,
    ByRegisterNotify = 1,
    ByProtocol = 2
} EFI_LOCATE_SEARCH_TYPE;
typedef EFI_STATUS(EFIAPI *EFI_LOCATE_HANDLE_BUFFER)(EFI_LOCATE_SEARCH_TYPE, EFI_GUID *, VOID *, UINTN *, EFI_HANDLE **);
typedef EFI_STATUS(EFIAPI *EFI_EXIT_BOOT_SERVICES)(EFI_HANDLE, UINTN);

typedef struct _EFI_BOOT_SERVICES
{
    EFI_TABLE_HEADER Hdr;
    /* Task Priority */ void *RaiseTPL;
    void *RestoreTPL;
    /* Memory */ EFI_ALLOCATE_PAGES AllocatePages;
    EFI_FREE_PAGES FreePages;
    EFI_GET_MEMORY_MAP GetMemoryMap;
    EFI_ALLOCATE_POOL AllocatePool;
    EFI_FREE_POOL FreePool;
    /* Event */ void *CreateEvent;
    void *SetTimer;
    void *WaitForEvent;
    void *SignalEvent;
    void *CloseEvent;
    void *CheckEvent;
    /* Protocol */ void *InstallProtocolInterface;
    void *ReinstallProtocolInterface;
    void *UninstallProtocolInterface;
    EFI_HANDLE_PROTOCOL HandleProtocol;
    void *Reserved;
    void *RegisterProtocolNotify;
    void *LocateHandle;
    void *LocateDevicePath;
    void *InstallConfigurationTable;
    /* Image */ void *LoadImage;
    void *StartImage;
    void *Exit;
    void *UnloadImage;
    EFI_EXIT_BOOT_SERVICES ExitBootServices;
    /* Misc */ void *GetNextMonotonicCount;
    void *Stall;
    void *SetWatchdogTimer;
    /* Driver */ void *ConnectController;
    void *DisconnectController;
    /* Open/Close */ void *OpenProtocol;
    void *CloseProtocol;
    void *OpenProtocolInformation;
    /* Library */ void *ProtocolsPerHandle;
    EFI_LOCATE_HANDLE_BUFFER LocateHandleBuffer;
    void *LocateProtocol;
    void *InstallMultipleProtocolInterfaces;
    void *UninstallMultipleProtocolInterfaces;
    /* CRC */ void *CalculateCrc32;
    /* Misc2 */ void *CopyMem;
    void *SetMem;
    void *CreateEventEx;
} EFI_BOOT_SERVICES;
