#include "../include/efi/system_table.h"
static EFI_BOOT_SERVICES *BS;
void UefiMemory_Init(EFI_SYSTEM_TABLE *ST) { BS = ST->BootServices; }
void *AllocPool(UINTN size)
{
    void *p = NULL;
    if (BS->AllocatePool(EfiLoaderData, size, &p) != EFI_SUCCESS)
        return NULL;
    return p;
}
void FreePool(void *p)
{
    if (p)
        BS->FreePool(p);
}
