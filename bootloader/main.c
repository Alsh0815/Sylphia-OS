#include "../uefi/uefi.h"

EFI_GUID gEfiGraphicsOutputProtocolGuid = {
    0x9042A9DE, 0x23DC, 0x4A38, {0x96, 0xFB, 0x7A, 0xDE, 0xD0, 0x80, 0x51, 0x6A}};
EFI_GUID gEfiLoadedImageProtocolGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
EFI_GUID gEfiSimpleFileSystemProtocolGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
EFI_GUID gEfiFileInfoGuid = EFI_FILE_INFO_ID;

EFI_STATUS EfiMain(VOID *ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_STATUS Status;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;

    // 1. GOP (Graphics Output Protocol) を探す
    Status = SystemTable->BootServices->LocateProtocol(
        &gEfiGraphicsOutputProtocolGuid,
        (VOID *)0,
        (VOID **)&Gop);

    if (Status != EFI_SUCCESS)
    {
        SystemTable->ConOut->OutputString(SystemTable->ConOut, (CHAR16 *)L"Error: GOP not found!\r\n");
        while (1)
            __asm__ volatile("hlt");
    }

    // 2. フレームバッファの情報を取得
    UINT32 *FrameBuffer = (UINT32 *)Gop->Mode->FrameBufferBase;
    UINTN FrameBufferSize = Gop->Mode->FrameBufferSize;
    UINT32 HorizontalResolution = Gop->Mode->Info->HorizontalResolution;
    UINT32 VerticalResolution = Gop->Mode->Info->VerticalResolution;

    // 3. 画面全体をオレンジ色で塗りつぶす (AARRGGBB形式)
    // オレンジ: R=255(0xFF), G=128(0x80), B=0(0x00)
    for (UINTN i = 0; i < VerticalResolution * HorizontalResolution; i++)
    {
        FrameBuffer[i] = 0xFFFF8000;
    }

    SystemTable->ConOut->OutputString(SystemTable->ConOut, (CHAR16 *)L"\r\nGetting Memory Map (Dynamic)...\r\n");

    // メモリマップ格納用変数の初期化
    UINTN MemoryMapSize = 0;
    EFI_MEMORY_DESCRIPTOR *MemoryMap = (void *)0;
    UINTN MapKey;
    UINTN DescriptorSize;
    UINT32 DescriptorVersion;

    // 1. まずサイズ取得のために一度呼び出す (Buffer=NULL, Size=0 で呼ぶ)
    Status = SystemTable->BootServices->GetMemoryMap(
        &MemoryMapSize,
        (void *)0,
        &MapKey,
        &DescriptorSize,
        &DescriptorVersion);

    // EFI_BUFFER_TOO_SMALL が返ってくるはず
    if (Status != 0x80000005)
    { // EFI_BUFFER_TOO_SMALL 以外ならエラーか、既に成功(ありえない)
      // エラー処理 (省略)
    }

    // 2. 必要なサイズ + 予備を確保して再試行するループ
    // (AllocatePool自体がメモリマップを断片化させてサイズが増える可能性があるためループする)
    while (1)
    {
        // 余裕を持たせる (Descriptor数個分)
        MemoryMapSize += DescriptorSize * 2;

        // メモリ確保 (EfiLoaderDataとして確保)
        Status = SystemTable->BootServices->AllocatePool(
            EfiLoaderData,
            MemoryMapSize,
            (VOID **)&MemoryMap);

        if (Status != EFI_SUCCESS)
        {
            SystemTable->ConOut->OutputString(SystemTable->ConOut, (CHAR16 *)L"AllocatePool Failed!\r\n");
            while (1)
                __asm__ volatile("hlt");
        }

        // 確保したバッファで再挑戦
        Status = SystemTable->BootServices->GetMemoryMap(
            &MemoryMapSize,
            MemoryMap,
            &MapKey,
            &DescriptorSize,
            &DescriptorVersion);

        if (Status == EFI_SUCCESS)
        {
            break; // 成功！
        }
        else if (Status == 0x80000005)
        { // まだ足りない (AllocatePoolでマップが育った)
            // 失敗したバッファは解放して、次へ
            SystemTable->BootServices->FreePool(MemoryMap);
            // ループ先頭に戻り、より大きなサイズで再確保
        }
        else
        {
            // その他のエラー
            SystemTable->ConOut->OutputString(SystemTable->ConOut, (CHAR16 *)L"GetMemoryMap Failed!\r\n");
            while (1)
                __asm__ volatile("hlt");
        }
    }

    SystemTable->ConOut->OutputString(SystemTable->ConOut, (CHAR16 *)L"Memory Map Get: SUCCESS with AllocatePool!\r\n");

    SystemTable->ConOut->OutputString(SystemTable->ConOut, (CHAR16 *)L"\r\nLoading kernel.elf...\r\n");

    // 1. Loaded Image Protocol を取得 (自分自身がどのデバイスから起動したかを知る)
    EFI_LOADED_IMAGE_PROTOCOL *LoadedImage;
    Status = SystemTable->BootServices->HandleProtocol(
        ImageHandle,
        &gEfiLoadedImageProtocolGuid,
        (VOID **)&LoadedImage);

    if (Status != EFI_SUCCESS)
    {
        SystemTable->ConOut->OutputString(SystemTable->ConOut, (CHAR16 *)L"Error: LoadedImage not found\r\n");
        while (1)
            __asm__ volatile("hlt");
    }

    // 2. そのデバイスの File System Protocol を開く
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;
    Status = SystemTable->BootServices->HandleProtocol(
        LoadedImage->DeviceHandle,
        &gEfiSimpleFileSystemProtocolGuid,
        (VOID **)&FileSystem);

    if (Status != EFI_SUCCESS)
    {
        SystemTable->ConOut->OutputString(SystemTable->ConOut, (CHAR16 *)L"Error: FileSystem not found\r\n");
        while (1)
            __asm__ volatile("hlt");
    }

    // 3. ルートディレクトリを開く
    EFI_FILE_PROTOCOL *Root;
    Status = FileSystem->OpenVolume(FileSystem, &Root);

    // 4. kernel.elf を開く
    EFI_FILE_PROTOCOL *KernelFile;
    Status = Root->Open(
        Root,
        &KernelFile,
        (CHAR16 *)L"kernel.elf",
        EFI_FILE_MODE_READ,
        0);

    if (Status != EFI_SUCCESS)
    {
        SystemTable->ConOut->OutputString(SystemTable->ConOut, (CHAR16 *)L"Error: kernel.elf not found!\r\n");
        while (1)
            __asm__ volatile("hlt");
    }

    // 5. ファイルサイズを取得する
    // サイズ取得用の構造体 + ファイル名の分くらいのバッファ
    UINTN FileInfoSize = sizeof(EFI_FILE_INFO) + sizeof(CHAR16) * 128;
    UINT8 FileInfoBuffer[256]; // スタックで十分
    EFI_FILE_INFO *FileInfo = (EFI_FILE_INFO *)FileInfoBuffer;

    Status = KernelFile->GetInfo(
        KernelFile,
        &gEfiFileInfoGuid,
        &FileInfoSize,
        FileInfo);

    // 6. カーネル格納用メモリを確保 (AllocatePool)
    VOID *KernelBuffer;
    Status = SystemTable->BootServices->AllocatePool(
        EfiLoaderData,
        FileInfo->FileSize,
        &KernelBuffer);

    // 7. ファイルを読み込む
    UINTN ReadSize = FileInfo->FileSize;
    Status = KernelFile->Read(KernelFile, &ReadSize, KernelBuffer);

    if (Status == EFI_SUCCESS)
    {
        SystemTable->ConOut->OutputString(SystemTable->ConOut, (CHAR16 *)L"Kernel Read Success. Parsing ELF Header...\r\n");

        // バッファをELFヘッダーとして解釈
        Elf64_Ehdr *Ehdr = (Elf64_Ehdr *)KernelBuffer;

        // 1. マジックナンバーのチェック ( \x7F E L F )
        if (Ehdr->e_ident[0] != 0x7F ||
            Ehdr->e_ident[1] != 'E' ||
            Ehdr->e_ident[2] != 'L' ||
            Ehdr->e_ident[3] != 'F')
        {

            SystemTable->ConOut->OutputString(SystemTable->ConOut, (CHAR16 *)L"Error: Not a valid ELF file!\r\n");
            while (1)
                __asm__ volatile("hlt");
        }

        // 2. エントリーポイントの表示 (ここが正しく出れば成功！)
        // 簡易的な数値表示ルーチンがないので、判定ロジックだけ入れます
        if (Ehdr->e_entry != 0)
        {
            SystemTable->ConOut->OutputString(SystemTable->ConOut, (CHAR16 *)L"ELF Header OK! Found Entry Point.\r\n");
        }

        // プログラムヘッダーテーブルの先頭アドレス
        Elf64_Phdr *Phdr = (Elf64_Phdr *)((UINT64)Ehdr + Ehdr->e_phoff);

        for (int i = 0; i < Ehdr->e_phnum; i++)
        {
            if (Phdr[i].p_type == PT_LOAD)
            {
                // メモリへのコピー (FileOffset -> VirtualAddress)
                CopyMem((VOID *)Phdr[i].p_vaddr,
                        (VOID *)((UINT64)Ehdr + Phdr[i].p_offset),
                        Phdr[i].p_filesz);

                // BSS領域 (ファイルサイズよりメモリサイズが大きい部分) を0で埋める
                UINTN RemainBytes = Phdr[i].p_memsz - Phdr[i].p_filesz;
                if (RemainBytes > 0)
                {
                    SetMem((VOID *)(Phdr[i].p_vaddr + Phdr[i].p_filesz), RemainBytes, 0);
                }
            }
        }

        SystemTable->ConOut->OutputString(SystemTable->ConOut, (CHAR16 *)L"Segments Loaded. Exiting Boot Services...\r\n");

        FrameBufferConfig Config;
        Config.FrameBufferBase = Gop->Mode->FrameBufferBase;
        Config.FrameBufferSize = Gop->Mode->FrameBufferSize;
        Config.HorizontalResolution = Gop->Mode->Info->HorizontalResolution;
        Config.VerticalResolution = Gop->Mode->Info->VerticalResolution;
        Config.PixelsPerScanLine = Gop->Mode->Info->PixelsPerScanLine;

        Status = SystemTable->BootServices->GetMemoryMap(
            &MemoryMapSize,
            MemoryMap,
            &MapKey,
            &DescriptorSize,
            &DescriptorVersion);

        Status = SystemTable->BootServices->ExitBootServices(ImageHandle, MapKey);
        if (Status != EFI_SUCCESS)
        {
            Status = SystemTable->BootServices->GetMemoryMap(
                &MemoryMapSize,
                MemoryMap,
                &MapKey,
                &DescriptorSize,
                &DescriptorVersion);
            SystemTable->BootServices->ExitBootServices(ImageHandle, MapKey);
        }

        typedef void (*KernelEntryPoint)(FrameBufferConfig *);
        KernelEntryPoint KernelMainStart = (KernelEntryPoint)Ehdr->e_entry;
        KernelMainStart(&Config);
        while (1)
            ;
    }
    else
    {
        SystemTable->ConOut->OutputString(SystemTable->ConOut, (CHAR16 *)L"Error: Read failed\r\n");
    }

    // ファイルハンドルを閉じる
    KernelFile->Close(KernelFile);
    Root->Close(Root);

    while (1)
    {
        __asm__ volatile("hlt");
    }

    return EFI_SUCCESS;
}