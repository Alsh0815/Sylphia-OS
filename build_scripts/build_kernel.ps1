$ErrorActionPreference = "Stop"

# ツールチェーン設定
$CLANG = "clang++"
$LD = "ld.lld" # LLVMに含まれるELF用リンカ

# ディレクトリ作成
New-Item -ItemType Directory -Force -Path "..\build" | Out-Null

Write-Host "Compiling Kernel (C++)..." -ForegroundColor Cyan

$SOURCES = @("..\kernel\main.cpp", "..\kernel\cxx.cpp", "..\kernel\new.cpp", "..\kernel\memory\memory_manager.cpp", "..\kernel\pci\pci.cpp", "..\kernel\shell\shell.cpp", "..\kernel\apic.cpp", "..\kernel\console.cpp", "..\kernel\font.cpp", "..\kernel\graphics.cpp", "..\kernel\interrupt.cpp", "..\kernel\ioapic.cpp", "..\kernel\keyboard.cpp", "..\kernel\pic.cpp", "..\kernel\printk.cpp", "..\kernel\segmentation.cpp")

# オブジェクトファイルのリスト格納用
$OBJECTS = @()

# 1. コンパイル (C++ -> OBJ)
# -target x86_64-elf : ELF形式を指定
# -ffreestanding : 標準ライブラリなし
# -fno-rtti -fno-exceptions : C++の動的機能を無効化 (OSカーネルには不要/邪魔)
foreach ($src in $SOURCES) {
    # 出力ファイル名を作成 (例: main.obj)
    $objName = [System.IO.Path]::GetFileNameWithoutExtension($src) + ".obj"
    $objPath = "..\build\$objName"
    $OBJECTS += $objPath

    & $CLANG -target x86_64-elf `
        -ffreestanding `
        -fno-rtti -fno-exceptions `
        -mno-red-zone `
        -mgeneral-regs-only `
        -I..\kernel `
        -c $src `
        -o $objPath
}
Write-Host "Linking Kernel (OBJ -> ELF)..." -ForegroundColor Cyan

# 2. リンク (OBJ -> ELF)
# -entry KernelMain : エントリーポイントを指定
# -z norelro : リロケーション情報を簡素化
# --image-base 0x100000 : カーネルをメモリの 1MB地点 に配置する前提
# -static : 静的リンク
& $LD -entry KernelMain `
    -z norelro `
    --image-base 0x100000 `
    --static `
    -o ..\build\kernel.elf `
    $OBJECTS

Write-Host "Kernel Build Success! Output: build\kernel.elf" -ForegroundColor Green