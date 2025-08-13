#include "../include/efi/protocols/gop.h"
#include "../include/efi/protocols/loaded_image.h"
#include "../include/efi/protocols/simple_fs.h"
#include "../include/efi/system_table.h"
#include "../include/elf64.h"
#include "../include/bootinfo.h"

/* ヘルパのプロトタイプ */
void UefiConsole_Init(EFI_SYSTEM_TABLE *);
void PutS(const CHAR16 *);
void PutLn(const CHAR16 *);
void PutU64(UINT64);
void UefiMemory_Init(EFI_SYSTEM_TABLE *);
void *AllocPool(UINTN);
void FreePool(void *);

static EFI_SYSTEM_TABLE *ST;
static EFI_BOOT_SERVICES *BS;

typedef void(__attribute__((sysv_abi)) * KernelEntry)(BootInfo *);

EFI_STATUS EFIAPI EfiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    ST = SystemTable;
    BS = ST->BootServices;
    UefiConsole_Init(ST);
    UefiMemory_Init(ST);

    ST->ConOut->ClearScreen(ST->ConOut);
    PutLn(L"[UEFI] Bare-metal UEFI app (no EDK II / no gnu-efi)");
    PutLn(L"Sylphia-OS Boot Loader");

    // FS 検出
    UINTN count = 0;
    EFI_HANDLE *handles = NULL;
    EFI_STATUS st = BS->LocateHandleBuffer(ByProtocol, (EFI_GUID *)&EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID, NULL, &count, &handles);
    if (EFI_ERROR(st) || count == 0)
    {
        PutLn(L"SimpleFileSystem not found.");
        return EFI_NOT_FOUND;
    }

    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *sfs = NULL;
    st = BS->HandleProtocol(handles[0], (EFI_GUID *)&EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID, (VOID **)&sfs);
    if (EFI_ERROR(st) || !sfs)
    {
        PutLn(L"HandleProtocol(SimpleFS) failed.");
        return st;
    }

    EFI_LOADED_IMAGE_PROTOCOL *li = NULL;
    st = BS->HandleProtocol(ImageHandle,
                            (EFI_GUID *)&EFI_LOADED_IMAGE_PROTOCOL_GUID,
                            (VOID **)&li);
    if (EFI_ERROR(st) || !li)
    {
        PutLn(L"HandleProtocol(LoadedImage) failed.");
        return st;
    }

    // 起動元デバイスの SimpleFS を取得
    st = BS->HandleProtocol(li->DeviceHandle,
                            (EFI_GUID *)&EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID,
                            (VOID **)&sfs);
    if (EFI_ERROR(st) || !sfs)
    {
        PutLn(L"HandleProtocol(SimpleFS from boot device) failed.");
        return st;
    }

    EFI_FILE_PROTOCOL *root = NULL;
    st = sfs->OpenVolume(sfs, &root);
    if (EFI_ERROR(st) || !root)
    {
        PutLn(L"OpenVolume failed.");
        return st;
    }

    // test.txt 書き込み→読み戻し
    EFI_FILE_PROTOCOL *file = NULL;
    st = root->Open(root, &file, L"\\test.txt", EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
    if (!EFI_ERROR(st) && file)
    {
        const CHAR16 *msg = L"hello from UEFI\r\n";
        UINTN bytes = StrLen16(msg) * sizeof(CHAR16);
        file->Write(file, &bytes, (const VOID *)msg);
        file->Close(file);
        PutLn(L"Wrote \\test.txt");
    }

    st = root->Open(root, &file, L"\\test.txt", EFI_FILE_MODE_READ, 0);
    if (!EFI_ERROR(st) && file)
    {
        UINTN cap = 512;
        CHAR16 *buf = AllocPool(cap);
        if (buf)
        {
            UINTN want = cap - sizeof(CHAR16);
            if (!EFI_ERROR(file->Read(file, &want, buf)))
            {
                buf[want / sizeof(CHAR16)] = 0;
                PutLn(L"Read back:");
                PutLn(buf);
            }
            FreePool(buf);
        }
        file->Close(file);
    }

    // kernel/kernel.bin 読み込み（サイズ→Read）
    EFI_FILE_PROTOCOL *kdir = NULL, *kfile = NULL;
    st = root->Open(root, &kdir, L"\\kernel", EFI_FILE_MODE_READ, 0);
    if (!EFI_ERROR(st) && kdir)
    {
        st = kdir->Open(kdir, &kfile, L"kernel.bin", EFI_FILE_MODE_READ, 0);
        if (!EFI_ERROR(st) && kfile)
        {
            UINTN infoSize = 0;
            st = kfile->GetInfo(kfile, (EFI_GUID *)&EFI_FILE_INFO_ID, &infoSize, NULL);
            if (st == EFI_BUFFER_TOO_SMALL)
            {
                EFI_FILE_INFO *info = AllocPool(infoSize);
                if (info && !EFI_ERROR(kfile->GetInfo(kfile, (EFI_GUID *)&EFI_FILE_INFO_ID, &infoSize, info)))
                {
                    PutS(L"\r\nkernel.bin size = ");
                    PutU64(info->FileSize);
                    PutLn(L" bytes");
                    VOID *kbuf = AllocPool((UINTN)info->FileSize);
                    if (kbuf)
                    {
                        UINTN sz = (UINTN)info->FileSize;
                        if (!EFI_ERROR(kfile->Read(kfile, &sz, kbuf)) && sz == (UINTN)info->FileSize)
                        {
                            EFI_PHYSICAL_ADDRESS entry = 0;
                            st = Elf64_LoadKernel(BS, kbuf, sz, &entry);
                            if (EFI_ERROR(st))
                            {
                                PutLn(L"ELF load failed.");
                                FreePool(kbuf);
                                goto done;
                            }

                            EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
                            EFI_STATUS st_gop = BS->LocateHandleBuffer(ByProtocol, (EFI_GUID *)&EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID,
                                                                       NULL, &count, &handles);
                            if (!EFI_ERROR(st_gop) && count > 0)
                            {
                                BS->HandleProtocol(handles[0], (EFI_GUID *)&EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID, (VOID **)&gop);
                            }

                            UINT32 kr_cnt = 0;
                            /* 一旦ヘッダだけ見て数える */
                            {
                                const UINT8 *base8 = (const UINT8 *)kbuf; // ※ kbuf に Read した ELF イメージ
                                const Elf64_Ehdr *eh = (const Elf64_Ehdr *)base8;
                                for (Elf64_Half i = 0; i < eh->e_phnum; ++i)
                                {
                                    const Elf64_Phdr *ph = (const Elf64_Phdr *)(base8 + eh->e_phoff + (UINTN)i * eh->e_phentsize);
                                    if (ph->p_type == PT_LOAD && ph->p_memsz)
                                        kr_cnt++;
                                }
                            }

                            /* 必要分の配列を確保（Exit 後も参照するので EfiLoaderData で OK） */
                            PhysRange *kr = NULL;
                            if (kr_cnt)
                            {
                                kr = (PhysRange *)AllocPool(sizeof(PhysRange) * kr_cnt);
                                if (!kr)
                                {
                                    PutLn(L"Alloc kernel ranges failed.");
                                    goto done;
                                }
                            }

                            /* 配列に詰める（p_vaddr 恒等配置 = 物理と同一前提）*/
                            UINT32 idx = 0;
                            {
                                const UINT8 *base8 = (const UINT8 *)kbuf;
                                const Elf64_Ehdr *eh = (const Elf64_Ehdr *)base8;
                                for (Elf64_Half i = 0; i < eh->e_phnum; ++i)
                                {
                                    const Elf64_Phdr *ph = (const Elf64_Phdr *)(base8 + eh->e_phoff + (UINTN)i * eh->e_phentsize);
                                    if (ph->p_type != PT_LOAD || ph->p_memsz == 0)
                                        continue;
                                    UINT64 vbeg = ph->p_vaddr & ~0xFFFULL;
                                    UINT64 vend = ((ph->p_vaddr + ph->p_memsz + 0xFFF) & ~0xFFFULL);
                                    kr[idx].base = vbeg;
                                    kr[idx].pages = (vend - vbeg) >> 12;
                                    idx++;
                                }
                            }

                            FreePool(kbuf);
                            PutLn(L"ELF loaded. Preparing to exit boot services...");

                            /* メモリマップ取得 → ExitBootServices（失敗時は再トライ） */
                            /* --- メモリマップ取得（ExitBootServicesに必要&カーネルへ渡す） --- */
                            UINTN mapSize = 0, mapKey = 0, descSize = 0;
                            UINT32 descVer = 0;

                            /* サイズ取得（0呼び） */
                            BS->GetMemoryMap(&mapSize, NULL, &mapKey, &descSize, &descVer);
                            mapSize += 2 * descSize;

                            /* バッファ確保（OS側で読み続けるので Exit 後も残る。EfiLoaderDataでOK） */
                            VOID *memmap = AllocPool(mapSize);
                            if (!memmap)
                            {
                                PutLn(L"Alloc memmap failed.");
                                goto done;
                            }

                            PutLn(L"BI: OK");

                            /* ループ: 取り直し→ExitBootServices */
                            for (;;)
                            {
                                EFI_STATUS mmst = BS->GetMemoryMap(&mapSize, memmap, &mapKey, &descSize, &descVer);
                                if (EFI_ERROR(mmst))
                                {
                                    PutLn(L"GetMemoryMap failed.");
                                    goto done;
                                }

                                /* BootInfo をこの時点で埋める（memmap内容は Exit 後も参照する） */
                                BootInfo bi = {0};
                                bi.magic = 0x534C504855454649ULL;
                                if (gop && gop->Mode && gop->Mode->Info)
                                {
                                    bi.fb_base = (UINT64)gop->Mode->FrameBufferBase;
                                    bi.fb_size = (UINT32)gop->Mode->FrameBufferSize;
                                    bi.width = gop->Mode->Info->HorizontalResolution;
                                    bi.height = gop->Mode->Info->VerticalResolution;
                                    bi.pitch = gop->Mode->Info->PixelsPerScanLine;
                                    bi.pixel_format = (UINT32)gop->Mode->Info->PixelFormat;
                                }
                                bi.mmap_ptr = (UINT64)(UINTN)memmap;
                                bi.mmap_size = (UINT64)mapSize;
                                bi.mmap_desc_size = (UINT32)descSize;
                                bi.mmap_desc_version = (UINT32)descVer;
                                bi.kernel_ranges_ptr = (UINT64)(UINTN)kr;
                                bi.kernel_ranges_cnt = kr_cnt;

                                /* ExitBootServices を試行 */
                                EFI_STATUS ex = BS->ExitBootServices(ImageHandle, mapKey);
                                if (EFI_ERROR(ex))
                                {
                                    /* 失敗: mapKeyが変わった可能性。バッファサイズを見て再トライ */
                                    BS->GetMemoryMap(&mapSize, memmap, &mapKey, &descSize, &descVer);
                                    continue;
                                }

                                /* ---- ここからUEFIサービス不可 ---- */
                                typedef void(__attribute__((sysv_abi)) * KernelEntry)(BootInfo *);
                                KernelEntry kentry = (KernelEntry)(UINTN)entry;
                                kentry(&bi);

                                break; /* 返ってきたら終了へ */
                            }
                        }
                        else
                        {
                            PutLn(L"kernel.bin read failed.");
                            FreePool(kbuf);
                            goto done;
                        }
                    }
                }
                if (info)
                    FreePool(info);
            }
            kfile->Close(kfile);
        }
        kdir->Close(kdir);
    }

done:
    PutLn(L"Done. Press any key...");
    EFI_INPUT_KEY key;
    ST->ConIn->ReadKeyStroke(ST->ConIn, &key);
    return EFI_SUCCESS;
}
