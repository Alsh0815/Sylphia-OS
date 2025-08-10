#include "../include/efi/system_table.h"
#include "../include/efi/protocols/simple_fs.h"

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
                            PutLn(L"kernel.bin loaded (no jump yet).");
                        }
                        FreePool(kbuf);
                    }
                }
                if (info)
                    FreePool(info);
            }
            kfile->Close(kfile);
        }
        kdir->Close(kdir);
    }

    PutLn(L"Done. Press any key...");
    EFI_INPUT_KEY key;
    ST->ConIn->ReadKeyStroke(ST->ConIn, &key);
    return EFI_SUCCESS;
}
