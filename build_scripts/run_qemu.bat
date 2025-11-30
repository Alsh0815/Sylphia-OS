@echo off
chcp 65001
setlocal

rem -----------------------------------------------------------
rem 設定エリア
rem -----------------------------------------------------------

set QEMU_PATH=D:\Program Files\qemu\qemu-system-x86_64.exe

set OVMF_CODE=OVMF_CODE.fd
set OVMF_VARS_ORIG=OVMF_VARS.fd
set OVMF_VARS_TMP=OVMF_VARS.copy.fd

set NVME_IMG=..\nvme.img

set BUILD_DIR=..\build

rem -----------------------------------------------------------

if not exist "%OVMF_CODE%" (
  echo [ERROR] %OVMF_CODE% not found in build_scripts!
  pause
  exit /b
)
if not exist "%OVMF_VARS_ORIG%" (
  echo [ERROR] %OVMF_VARS_ORIG% not found in build_scripts!
  pause
  exit /b
)

echo Syncing USB VHD image...
powershell -NoProfile -ExecutionPolicy Bypass -File "sync_usb_vhd.ps1"

copy /y "%OVMF_VARS_ORIG%" "%OVMF_VARS_TMP%" > nul

echo Starting Sylphia-OS in QEMU (MikanOS OVMF style)...

"%QEMU_PATH%" ^
-m 512M ^
-drive if=pflash,format=raw,readonly=on,file=%OVMF_CODE% ^
-drive if=pflash,format=raw,file=%OVMF_VARS_TMP% ^
-drive file=..\usb.vhd,format=vpc,if=none,id=usbstick ^
-drive file=%NVME_IMG%,if=none,id=nvm ^
-device nvme,serial=deadbeef,drive=nvm ^
-device qemu-xhci,id=xhci ^
-device usb-storage,bus=xhci.0,drive=usbstick,bootindex=1 ^
-device usb-kbd ^
-net none ^
-monitor stdio ^
-d int -D qemu.log

del "%OVMF_VARS_TMP%"

endlocal
