param (
    [Parameter(Mandatory = $true)]
    [string]$AppName
)

$ErrorActionPreference = "Stop"

$RepoRoot = Resolve-Path "$PSScriptRoot/.."
$AppDir = "$RepoRoot/apps/$AppName"
$OutputDir = "$RepoRoot/build/apps"

$CLANG = "clang++"
$LD = "ld.lld"
$NASM = "nasm"

if (-not (Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir | Out-Null
}

Write-Host "Building application: $AppName" -ForegroundColor Cyan

$StartObj = "$AppDir/start.o"
if (Test-Path "$AppDir/start.asm") {
    & $NASM -f elf64 "$AppDir/start.asm" -o $StartObj
}
else {
    Write-Error "start.asm not found in $AppDir"
}

$CppSrcs = Get-ChildItem "$AppDir/*.cpp"
$CppObjs = @()

foreach ($Src in $CppSrcs) {
    $Obj = "$AppDir/" + $Src.BaseName + ".o"
    $CppObjs += $Obj
    & $CLANG -c $Src.FullName -o $Obj -target x86_64-pc-none-elf -ffreestanding -fno-rtti -fno-exceptions -O2 -Wall
}

$OutputElf = "$OutputDir/$AppName.elf"
$LinkerScript = "$AppDir/linker.ld"

$AllObjs = @($StartObj) + $CppObjs

if (Test-Path $LinkerScript) {
    & $LD -T $LinkerScript -o $OutputElf $AllObjs --entry _start
    # & $LD -T $LinkerScript -o $OutputElf $AllObjs --entry _start -z separate-code
}
else {
    Write-Error "linker.ld not found in $AppDir"
}

Write-Host "Build Complete! Output: $OutputElf" -ForegroundColor Green