#include "../include/efi/system_table.h"

static EFI_SYSTEM_TABLE *ST;
void UefiConsole_Init(EFI_SYSTEM_TABLE *SystemTable) { ST = SystemTable; }

void PutS(const CHAR16 *s) { ST->ConOut->OutputString(ST->ConOut, (CHAR16 *)s); }
void PutLn(const CHAR16 *s)
{
    ST->ConOut->OutputString(ST->ConOut, (CHAR16 *)s);
    ST->ConOut->OutputString(ST->ConOut, L"\r\n");
}

void PutHex64(UINT64 v)
{
    CHAR16 hex[17];
    const CHAR16 *d = L"0123456789ABCDEF";
    for (int i = 15; i >= 0; i--)
    {
        hex[i] = d[v & 0xF];
        v >>= 4;
    }
    hex[16] = 0;
    PutS(L"0x");
    ST->ConOut->OutputString(ST->ConOut, hex);
}

void PutU64(UINT64 v)
{
    CHAR16 tmp[32];
    UINTN i = 0;
    if (v == 0)
    {
        PutS(L"0");
        return;
    }
    while (v > 0 && i < 31)
    {
        tmp[i++] = (CHAR16)(L'0' + (v % 10));
        v /= 10;
    }
    CHAR16 out[34];
    UINTN j = 0;
    while (i > 0)
        out[j++] = tmp[--i];
    out[j] = 0;
    PutS(out);
}
