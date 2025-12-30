#include "../uefi/uefi.h"
#include <stddef.h>

void *memset(void *s, int c, size_t n)
{
    unsigned char *p = (unsigned char *)s;
    while (n--)
        *p++ = (unsigned char)c;
    return s;
}

EFI_GUID gEfiGraphicsOutputProtocolGuid = {
    0x9042A9DE,
    0x23DC,
    0x4A38,
    {0x96, 0xFB, 0x7A, 0xDE, 0xD0, 0x80, 0x51, 0x6A}};
EFI_GUID gEfiLoadedImageProtocolGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
EFI_GUID gEfiSimpleFileSystemProtocolGuid =
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
EFI_GUID gEfiFileInfoGuid = EFI_FILE_INFO_ID;

EFI_STATUS EfiMain(VOID *ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_STATUS Status;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;

    Status = SystemTable->BootServices->LocateProtocol(
        &gEfiGraphicsOutputProtocolGuid, (VOID *)0, (VOID **)&Gop);

    if (Status != EFI_SUCCESS)
    {
        SystemTable->ConOut->OutputString(
            SystemTable->ConOut, (CHAR16 *)L"Error: GOP not found!\r\n");
        while (1)
#if defined(__x86_64__)
            __asm__ volatile("hlt");
#elif defined(__aarch64__)
            __asm__ volatile("wfe");
#endif
    }

    UINT32 *FrameBuffer = (UINT32 *)Gop->Mode->FrameBufferBase;
    UINTN FrameBufferSize = Gop->Mode->FrameBufferSize;
    UINT32 HorizontalResolution = Gop->Mode->Info->HorizontalResolution;
    UINT32 VerticalResolution = Gop->Mode->Info->VerticalResolution;

    for (UINTN i = 0; i < VerticalResolution * HorizontalResolution; i++)
    {
        FrameBuffer[i] = 0xFFFF8000;
    }

    SystemTable->ConOut->OutputString(
        SystemTable->ConOut,
        (CHAR16 *)L"\r\nGetting Memory Map (Dynamic)...\r\n");

    UINTN MemoryMapSize = 0;
    EFI_MEMORY_DESCRIPTOR *MemoryMap = (void *)0;
    UINTN MapKey;
    UINTN DescriptorSize;
    UINT32 DescriptorVersion;

    Status = SystemTable->BootServices->GetMemoryMap(&MemoryMapSize, (void *)0,
                                                     &MapKey, &DescriptorSize,
                                                     &DescriptorVersion);

    if (Status != 0x80000005)
    {
    }

    while (1)
    {
        MemoryMapSize += 4096;

        Status = SystemTable->BootServices->AllocatePool(
            EfiLoaderData, MemoryMapSize, (VOID **)&MemoryMap);

        if (Status != EFI_SUCCESS)
        {
            SystemTable->ConOut->OutputString(
                SystemTable->ConOut, (CHAR16 *)L"AllocatePool Failed!\r\n");
            while (1)
#if defined(__x86_64__)
                __asm__ volatile("hlt");
#elif defined(__aarch64__)
                __asm__ volatile("wfe");
#endif
        }

        Status = SystemTable->BootServices->GetMemoryMap(
            &MemoryMapSize, MemoryMap, &MapKey, &DescriptorSize,
            &DescriptorVersion);

        if (Status == EFI_SUCCESS)
        {
            break;
        }
        else if (Status == 0x80000005)
        {
            SystemTable->BootServices->FreePool(MemoryMap);
        }
        else
        {
            SystemTable->ConOut->OutputString(
                SystemTable->ConOut, (CHAR16 *)L"GetMemoryMap Failed!\r\n");
            while (1)
#if defined(__x86_64__)
                __asm__ volatile("hlt");
#elif defined(__aarch64__)
                __asm__ volatile("wfe");
#endif
        }
    }

    SystemTable->ConOut->OutputString(
        SystemTable->ConOut,
        (CHAR16 *)L"Memory Map Get: SUCCESS with AllocatePool!\r\n");

    SystemTable->ConOut->OutputString(
        SystemTable->ConOut, (CHAR16 *)L"\r\nLoading kernel.elf...\r\n");

    EFI_LOADED_IMAGE_PROTOCOL *LoadedImage;
    Status = SystemTable->BootServices->HandleProtocol(
        ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&LoadedImage);

    if (Status != EFI_SUCCESS)
    {
        SystemTable->ConOut->OutputString(
            SystemTable->ConOut, (CHAR16 *)L"Error: LoadedImage not found\r\n");
        while (1)
#if defined(__x86_64__)
            __asm__ volatile("hlt");
#elif defined(__aarch64__)
            __asm__ volatile("wfe");
#endif
    }

    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;
    Status = SystemTable->BootServices->HandleProtocol(
        LoadedImage->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid,
        (VOID **)&FileSystem);

    if (Status != EFI_SUCCESS)
    {
        SystemTable->ConOut->OutputString(
            SystemTable->ConOut, (CHAR16 *)L"Error: FileSystem not found\r\n");
        while (1)
#if defined(__x86_64__)
            __asm__ volatile("hlt");
#elif defined(__aarch64__)
            __asm__ volatile("wfe");
#endif
    }

    EFI_FILE_PROTOCOL *Root;
    Status = FileSystem->OpenVolume(FileSystem, &Root);

    EFI_FILE_PROTOCOL *KernelFile;
    Status = Root->Open(Root, &KernelFile, (CHAR16 *)L"kernel.elf",
                        EFI_FILE_MODE_READ, 0);

    if (Status != EFI_SUCCESS)
    {
        SystemTable->ConOut->OutputString(
            SystemTable->ConOut, (CHAR16 *)L"Error: kernel.elf not found!\r\n");
        while (1)
#if defined(__x86_64__)
            __asm__ volatile("hlt");
#elif defined(__aarch64__)
            __asm__ volatile("wfe");
#endif
    }

    VOID *HeaderBuffer;
    UINTN HeaderBufferSize = 4096;
    Status = SystemTable->BootServices->AllocatePool(
        EfiLoaderData, HeaderBufferSize, &HeaderBuffer);

    UINTN ReadSizeHeader = HeaderBufferSize;
    KernelFile->Read(KernelFile, &ReadSizeHeader, HeaderBuffer);

    Elf64_Ehdr *Ehdr = (Elf64_Ehdr *)HeaderBuffer;

    if (Ehdr->e_ident[0] != 0x7F || Ehdr->e_ident[1] != 'E' ||
        Ehdr->e_ident[2] != 'L' || Ehdr->e_ident[3] != 'F')
    {
        SystemTable->ConOut->OutputString(
            SystemTable->ConOut, (CHAR16 *)L"Error: Not a valid ELF file!\r\n");
        while (1)
#if defined(__x86_64__)
            __asm__ volatile("hlt");
#elif defined(__aarch64__)
            __asm__ volatile("wfe");
#endif
    }

    UINT64 KernelFirst = 0xFFFFFFFFFFFFFFFF;
    UINT64 KernelLast = 0;
    Elf64_Phdr *Phdr = (Elf64_Phdr *)((UINT64)Ehdr + Ehdr->e_phoff);

    for (int i = 0; i < Ehdr->e_phnum; i++)
    {
        if (Phdr[i].p_type == PT_LOAD)
        {
            if (Phdr[i].p_vaddr < KernelFirst)
                KernelFirst = Phdr[i].p_vaddr;
            UINT64 EndAddr = Phdr[i].p_vaddr + Phdr[i].p_memsz;
            if (EndAddr > KernelLast)
                KernelLast = EndAddr;
        }
    }

    UINTN NumPages = (KernelLast - KernelFirst + 0xFFF) / 0x1000;
    EFI_PHYSICAL_ADDRESS KernelBaseAddr = KernelFirst; // 0x100000

#if defined(__x86_64__)
    Status = SystemTable->BootServices->AllocatePages(
        AllocateAddress, EfiLoaderCode, NumPages, &KernelBaseAddr);
#elif defined(__aarch64__)
    Status = SystemTable->BootServices->AllocatePages(
        AllocateAnyPages, EfiLoaderCode, NumPages, &KernelBaseAddr);
#endif

    if (Status != EFI_SUCCESS)
    {
        SystemTable->ConOut->OutputString(
            SystemTable->ConOut,
            (CHAR16 *)L"Error: Failed to allocate kernel memory.\r\n");
        while (1)
#if defined(__x86_64__)
            __asm__ volatile("hlt");
#elif defined(__aarch64__)
            __asm__ volatile("wfe");
#endif
    }

    UINT64 Delta = 0;
#if defined(__aarch64__)
    Delta = KernelBaseAddr - KernelFirst;
#endif

    SystemTable->BootServices->FreePool(HeaderBuffer);

    UINTN FileInfoSize = sizeof(EFI_FILE_INFO) + sizeof(CHAR16) * 128;
    UINT8 FileInfoBuffer[256];
    EFI_FILE_INFO *FileInfo = (EFI_FILE_INFO *)FileInfoBuffer;

    Status = KernelFile->GetInfo(KernelFile, &gEfiFileInfoGuid, &FileInfoSize,
                                 FileInfo);

    VOID *KernelBuffer;
    Status = SystemTable->BootServices->AllocatePool(
        EfiLoaderData, FileInfo->FileSize, &KernelBuffer);

    if (Status != EFI_SUCCESS)
    {
        SystemTable->ConOut->OutputString(
            SystemTable->ConOut,
            (CHAR16 *)L"Error: Failed to allocate file buffer.\r\n");
        while (1)
#if defined(__x86_64__)
            __asm__ volatile("hlt");
#elif defined(__aarch64__)
            __asm__ volatile("wfe");
#endif
    }

    KernelFile->SetPosition(KernelFile, 0);
    UINTN ReadSize = FileInfo->FileSize;
    Status = KernelFile->Read(KernelFile, &ReadSize, KernelBuffer);

    if (Status != EFI_SUCCESS)
    {
        SystemTable->ConOut->OutputString(SystemTable->ConOut,
                                          (CHAR16 *)L"Error: Read failed\r\n");
        while (1)
#if defined(__x86_64__)
            __asm__ volatile("hlt");
#elif defined(__aarch64__)
            __asm__ volatile("wfe");
#endif
    }

    SystemTable->ConOut->OutputString(
        SystemTable->ConOut,
        (CHAR16 *)L"Kernel Read Success. Loading Segments...\r\n");

    Ehdr = (Elf64_Ehdr *)KernelBuffer;
    Phdr = (Elf64_Phdr *)((UINT64)Ehdr + Ehdr->e_phoff);

    for (int i = 0; i < Ehdr->e_phnum; i++)
    {
        if (Phdr[i].p_type == PT_LOAD)
        {
            CopyMem((VOID *)(Phdr[i].p_vaddr + Delta),
                    (VOID *)((UINT64)Ehdr + Phdr[i].p_offset),
                    Phdr[i].p_filesz);

            UINTN RemainBytes = Phdr[i].p_memsz - Phdr[i].p_filesz;
            if (RemainBytes > 0)
            {
                SetMem((VOID *)(Phdr[i].p_vaddr + Phdr[i].p_filesz + Delta),
                       RemainBytes, 0);
            }
        }
    }

    SystemTable->ConOut->OutputString(
        SystemTable->ConOut,
        (CHAR16 *)L"Segments Loaded. Exiting Boot Services...\r\n");

    FrameBufferConfig Config;
    Config.FrameBufferBase = Gop->Mode->FrameBufferBase;
    Config.FrameBufferSize = Gop->Mode->FrameBufferSize;
    Config.HorizontalResolution = Gop->Mode->Info->HorizontalResolution;
    Config.VerticalResolution = Gop->Mode->Info->VerticalResolution;
    Config.PixelsPerScanLine = Gop->Mode->Info->PixelsPerScanLine;

    if (MemoryMap)
    {
        SystemTable->BootServices->FreePool(MemoryMap);
        MemoryMap = NULL;
    }
    MemoryMapSize = 0;

    Status = SystemTable->BootServices->GetMemoryMap(&MemoryMapSize, (void *)0,
                                                     &MapKey, &DescriptorSize,
                                                     &DescriptorVersion);

    UINTN BufferSize = MemoryMapSize + 4096;

    while (1)
    {
        BufferSize += 4096;

        Status = SystemTable->BootServices->AllocatePool(
            EfiLoaderData, BufferSize, (VOID **)&MemoryMap);

        if (Status != EFI_SUCCESS)
        {
            SystemTable->ConOut->OutputString(
                SystemTable->ConOut,
                (CHAR16 *)L"AllocatePool Failed at Exit\r\n");
            while (1)
#if defined(__x86_64__)
                __asm__ volatile("hlt");
#elif defined(__aarch64__)
                __asm__ volatile("wfe");
#endif
        }

        MemoryMapSize = BufferSize;

        Status = SystemTable->BootServices->GetMemoryMap(
            &MemoryMapSize, MemoryMap, &MapKey, &DescriptorSize,
            &DescriptorVersion);

        if (Status == EFI_SUCCESS)
        {
            break;
        }
        else
        {
            SystemTable->BootServices->FreePool(MemoryMap);
            MemoryMap = NULL;
        }
    }

    Status = SystemTable->BootServices->ExitBootServices(ImageHandle, MapKey);
    if (Status != EFI_SUCCESS)
    {
        MemoryMapSize = BufferSize;
        Status = SystemTable->BootServices->GetMemoryMap(
            &MemoryMapSize, MemoryMap, &MapKey, &DescriptorSize,
            &DescriptorVersion);

        if (Status != EFI_SUCCESS)
        {
            SystemTable->ConOut->OutputString(
                SystemTable->ConOut,
                (CHAR16 *)L"Final GetMemoryMap Failed!\r\n");
            while (1)
#if defined(__x86_64__)
                __asm__ volatile("hlt");
#elif defined(__aarch64__)
                __asm__ volatile("wfe");
#endif
        }

        SystemTable->BootServices->ExitBootServices(ImageHandle, MapKey);
    }

    struct MemoryMap memmap_arg;
    memmap_arg.buffer_size = MemoryMapSize;
    memmap_arg.buffer = MemoryMap;
    memmap_arg.map_size = MemoryMapSize;
    memmap_arg.map_key = MapKey;
    memmap_arg.descriptor_size = DescriptorSize;
    memmap_arg.descriptor_version = DescriptorVersion;

    typedef void (*KernelEntryPoint)(FrameBufferConfig *, struct MemoryMap *);
    KernelEntryPoint KernelMainStart =
        (KernelEntryPoint)(Ehdr->e_entry + Delta);

    KernelMainStart(&Config, &memmap_arg);
    while (1)
        ;

    Root->Close(Root);

    while (1)
    {
        __asm__ volatile("hlt");
    }

    return EFI_SUCCESS;
}