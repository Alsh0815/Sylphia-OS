#include "../include/elf64.h"
#include "../include/efi/base.h"

static UINT64 AlignUp(UINT64 v, UINT64 a) { return (v + a - 1) & ~(a - 1); }

EFI_STATUS Elf64_LoadKernel(
    EFI_BOOT_SERVICES *BS,
    const void *img, UINTN size,
    EFI_PHYSICAL_ADDRESS *entry_out)
{
    if (!img || size < sizeof(Elf64_Ehdr))
        return EFI_LOAD_ERROR;

    const Elf64_Ehdr *eh = (const Elf64_Ehdr *)img;
    if (!(eh->e_ident[0] == 0x7F && eh->e_ident[1] == 'E' && eh->e_ident[2] == 'L' && eh->e_ident[3] == 'F'))
        return EFI_LOAD_ERROR;
    if (eh->e_phoff + (UINT64)eh->e_phnum * eh->e_phentsize > size)
        return EFI_LOAD_ERROR;

    const UINT8 *base = (const UINT8 *)img;

    for (Elf64_Half i = 0; i < eh->e_phnum; i++)
    {
        const Elf64_Phdr *ph = (const Elf64_Phdr *)(base + eh->e_phoff + (UINT64)i * eh->e_phentsize);
        if (ph->p_type != PT_LOAD || ph->p_memsz == 0)
            continue;

        UINT64 vbeg = ph->p_vaddr & ~0xFFFULL;
        UINT64 vend = AlignUp(ph->p_vaddr + ph->p_memsz, 0x1000);
        UINT64 pages = (vend - vbeg) >> 12;
        EFI_PHYSICAL_ADDRESS addr = (EFI_PHYSICAL_ADDRESS)vbeg;

        /* 恒等配置（要求アドレスに確保） */
        EFI_STATUS st = BS->AllocatePages(AllocateAddress, EfiLoaderData, pages, &addr);
        if (EFI_ERROR(st))
            return st;

        UINT8 *dst = (UINT8 *)(UINTN)ph->p_vaddr;
        const UINT8 *src = base + ph->p_offset;
        UINT64 filesz = ph->p_filesz, memsz = ph->p_memsz;

        for (UINT64 k = 0; k < filesz; k++)
            dst[k] = src[k];
        for (UINT64 k = filesz; k < memsz; k++)
            dst[k] = 0;
    }

    *entry_out = (EFI_PHYSICAL_ADDRESS)eh->e_entry;
    return EFI_SUCCESS;
}
