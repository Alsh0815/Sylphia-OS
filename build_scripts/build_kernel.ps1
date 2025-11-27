$ErrorActionPreference = "Stop"

$CLANG = "clang++"
$LD = "ld.lld"
$NASM = "nasm"

New-Item -ItemType Directory -Force -Path "..\build" | Out-Null

Write-Host "Compiling Kernel (C++)..." -ForegroundColor Cyan

$ASM_SOURCES = @("..\kernel\asmfunc.asm")
$SOURCES = @(
    "..\kernel\main.cpp", "..\kernel\cxx.cpp", "..\kernel\new.cpp",
    "..\kernel\driver\nvme\nvme_driver.cpp",
    "..\kernel\fs\fat32\fat32_driver.cpp", "..\kernel\fs\fat32\fat32.cpp", "..\kernel\fs\gpt.cpp", "..\kernel\fs\installer.cpp",
    "..\kernel\memory\memory_manager.cpp",
    "..\kernel\pci\pci.cpp",
    "..\kernel\shell\shell.cpp",
    "..\kernel\apic.cpp", "..\kernel\console.cpp", "..\kernel\font.cpp", "..\kernel\graphics.cpp",
    "..\kernel\interrupt.cpp", "..\kernel\ioapic.cpp", "..\kernel\keyboard.cpp", "..\kernel\paging.cpp",
    "..\kernel\pic.cpp", "..\kernel\printk.cpp", "..\kernel\segmentation.cpp"
)

$OBJECTS = @()

Write-Host "Compiling Assembly (NASM)..." -ForegroundColor Cyan

foreach ($src in $ASM_SOURCES) {
    if (Test-Path $src) {
        $objName = [System.IO.Path]::GetFileNameWithoutExtension($src) + ".obj"
        $objPath = "..\build\$objName"
        $OBJECTS += $objPath
        & $NASM -f elf64 $src -o $objPath
    }
    else {
        Write-Warning "Assembly file not found: $src (Skipping...)"
    }
}

foreach ($src in $SOURCES) {
    $objName = [System.IO.Path]::GetFileNameWithoutExtension($src) + ".obj"
    $objPath = "..\build\$objName"
    $OBJECTS += $objPath

    & $CLANG -target x86_64-elf `
        -ffreestanding `
        -fno-rtti -fno-exceptions `
        -mno-red-zone `
        -mgeneral-regs-only `
        -I.. `
        -I..\kernel `
        -c $src `
        -o $objPath
}
Write-Host "Linking Kernel (OBJ -> ELF)..." -ForegroundColor Cyan

& $LD -entry KernelMain `
    -z norelro `
    --image-base 0x100000 `
    --static `
    -o ..\build\kernel.elf `
    $OBJECTS

Write-Host "Kernel Build Success! Output: build\kernel.elf" -ForegroundColor Green