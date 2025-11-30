param (
    [string]$BuildDir = "../build",
    [string]$VhdPath = "../usb.vhd",
    [string]$DiskLetter = "V"
)

$currentPrincipal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $currentPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host "Requesting Administrator privileges..." -ForegroundColor Yellow
    try {
        $process = Start-Process powershell.exe -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`"" -Verb RunAs -PassThru -Wait -WorkingDirectory $PSScriptRoot
        exit $process.ExitCode
    }
    catch {
        Write-Error "Failed to elevate privileges. Please run as Administrator."
        exit 1
    }
}

Set-Location $PSScriptRoot

$AbsBuildDir = Resolve-Path $BuildDir
$AbsVhdPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($VhdPath)

if (-not (Test-Path $AbsVhdPath)) {
    Write-Host "Creating new FAT32 VHD: $AbsVhdPath"

    $dps = @"
create vdisk file="$AbsVhdPath" maximum=256 type=fixed
select vdisk file="$AbsVhdPath"
attach vdisk
create partition primary
format fs=fat32 quick label="SYLPHIA_USB"
assign letter=$DiskLetter
exit
"@
    $dps | Out-File -Encoding ASCII "diskpart_create.txt"
    diskpart /s "diskpart_create.txt" | Out-Null
    Remove-Item "diskpart_create.txt"
}
else {
    Write-Host "Mounting VHD..."
    $dps = @"
select vdisk file="$AbsVhdPath"
attach vdisk
assign letter=$DiskLetter
exit
"@
    $dps | Out-File -Encoding ASCII "diskpart_mount.txt"
    diskpart /s "diskpart_mount.txt" | Out-Null
    Remove-Item "diskpart_mount.txt"
}

Start-Sleep -Seconds 1

if (-not (Test-Path "${DiskLetter}:\")) {
    Write-Error "Failed to mount VHD to drive ${DiskLetter}:"
    exit 1
}

Write-Host "Syncing files from $AbsBuildDir to ${DiskLetter}:\"
robocopy $AbsBuildDir "${DiskLetter}:\" /MIR /NFL /NDL /NJH /NJS /nc /ns /np

Write-Host "Unmounting VHD..."
$dps = @"
select vdisk file="$AbsVhdPath"
detach vdisk
exit
"@
$dps | Out-File -Encoding ASCII "diskpart_unmount.txt"
diskpart /s "diskpart_unmount.txt" | Out-Null
Remove-Item "diskpart_unmount.txt"

Write-Host "Done."