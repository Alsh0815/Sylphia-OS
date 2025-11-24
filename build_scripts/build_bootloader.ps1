# エラーが発生したら停止
$ErrorActionPreference = "Stop"

$CLANG = "clang"
$LLD_LINK = "lld-link"

# ディレクトリ作成
New-Item -ItemType Directory -Force -Path "..\build\EFI\BOOT" | Out-Null

Write-Host "Compiling Bootloader..." -ForegroundColor Cyan

# 1. コンパイル (C -> OBJ)
# -target x86_64-pc-win32-coff : UEFIはWindowsと同じPE形式のバイナリが必要
# -fno-stack-protector : 標準ライブラリがないためスタック保護機能を無効化
# -fshort-wchar : UEFIのCHAR16に合わせてwchar_tを16bitにする
# -mno-red-zone : x64の割り込みハンドラ等でのスタック破壊を防ぐ（OS開発の必須オプション）
& $CLANG -target x86_64-pc-win32-coff `
    -fno-stack-protector `
    -fshort-wchar `
    -mno-red-zone `
    -c ..\bootloader\main.c `
    -o ..\build\main.obj
    
# 2. リンク (OBJ -> EFI)
# /subsystem:efi_application : UEFIアプリであることを指定
# /entry:EfiMain : エントリーポイントを指定
# /dll : EFIファイルはDLL構造を持つ
& $LLD_LINK /subsystem:efi_application `
    /entry:EfiMain `
    /dll `
    /out:..\build\EFI\BOOT\BOOTX64.EFI `
    ..\build\main.obj

Write-Host "Build Success! Output: build\EFI\BOOT\BOOTX64.EFI" -ForegroundColor Green