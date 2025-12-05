$ErrorActionPreference = "Stop"

$CLANG = "clang"
$CLANGP = "clang++"
$LD = "ld.lld"
$NASM = "nasm"

New-Item -ItemType Directory -Force -Path "..\build" | Out-Null

$ASM_SOURCES = @("..\kernel\asmfunc.asm")
$SOURCES = @(
    "..\kernel\main.cpp", "..\kernel\cxx.cpp", "..\kernel\new.cpp",
    "..\kernel\app\elf\elf_loader.cpp",
    "..\kernel\driver\nvme\nvme_driver.cpp",
    "..\kernel\driver\usb\keyboard\keyboard.cpp", "..\kernel\driver\usb\mass_storage\mass_storage.cpp", "..\kernel\driver\usb\xhci.cpp",
    "..\kernel\fs\fat32\fat32_driver.cpp", "..\kernel\fs\fat32\fat32.cpp", "..\kernel\fs\gpt.cpp", "..\kernel\fs\installer.cpp",
    "..\kernel\memory\memory_manager.cpp",
    "..\kernel\pci\pci.cpp",
    "..\kernel\shell\shell.cpp",
    "..\kernel\sys\logger\logger.cpp",
    "..\kernel\sys\std\file_descriptor.cpp",
    "..\kernel\sys\sys.cpp",
    "..\kernel\sys\syscall.cpp",
    "..\kernel\apic.cpp", "..\kernel\console.cpp", "..\kernel\font.cpp", "..\kernel\graphics.cpp",
    "..\kernel\interrupt.cpp", "..\kernel\ioapic.cpp", "..\kernel\keyboard_layout.cpp", "..\kernel\paging.cpp",
    "..\kernel\pic.cpp", "..\kernel\printk.cpp", "..\kernel\segmentation.cpp"
)

$STD_SOURCES = @(
    "..\std\string.cpp"
)

$OBJECTS = @()

Write-Host "Cleaning previous build artifacts..." -ForegroundColor Cyan

Get-ChildItem -Path "..\build" -Recurse -Filter "*.obj" | Remove-Item -Force

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

Write-Host "Compiling Kernel (C++)..." -ForegroundColor Cyan

foreach ($src in $SOURCES) {
    $objName = [System.IO.Path]::GetFileNameWithoutExtension($src) + ".obj"
    $objPath = "..\build\$objName"
    $OBJECTS += $objPath

    $result = & $CLANGP -target x86_64-elf `
        -ffreestanding `
        -fno-rtti -fno-exceptions `
        -mno-red-zone `
        -mgeneral-regs-only `
        -I.. `
        -I..\kernel `
        -c $src `
        -o $objPath 2>&1
    
    if (-not $result) {
        Write-Host "- OK ($src)" -ForegroundColor Cyan
    }
    else {
        Write-Host "- ERROR ($src)" -ForegroundColor Red
        $result
    }
}

Write-Host "Compiling Standard Library (C++)..." -ForegroundColor Cyan
foreach ($src in $STD_SOURCES) {
    $objName = [System.IO.Path]::GetFileNameWithoutExtension($src) + ".obj"
    $objPath = "..\build\$objName"
    $OBJECTS += $objPath

    $result = & $CLANG -target x86_64-elf `
        -ffreestanding `
        -fno-rtti -fno-exceptions `
        -mno-red-zone `
        -mgeneral-regs-only `
        -c $src `
        -o $objPath 2>&1

    if (-not $result) {
        Write-Host "- OK ($src)" -ForegroundColor Cyan
    }
    else {
        Write-Host "- ERROR ($src)" -ForegroundColor Red
        $result
    }
}

Write-Host "Linking Kernel (OBJ -> ELF)..." -ForegroundColor Cyan

& $LD -entry KernelMain `
    -z norelro `
    -T ..\kernel\kernel.ld `
    --static `
    -o ..\build\kernel.elf `
    $OBJECTS

Get-ChildItem -Path "..\build" -Recurse -Filter "*.obj" | Remove-Item -Force

Write-Host "Kernel Build Success! Output: build\kernel.elf" -ForegroundColor Green