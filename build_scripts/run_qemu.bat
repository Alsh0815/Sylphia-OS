@echo off
chcp 65001
setlocal

rem -----------------------------------------------------------
rem 設定エリア
rem -----------------------------------------------------------

set QEMU_PATH=D:\Program Files\qemu\qemu-system-x86_64.exe

rem OVMFファイル名
set OVMF_CODE=OVMF_CODE.fd
set OVMF_VARS_ORIG=OVMF_VARS.fd
set OVMF_VARS_TMP=OVMF_VARS.copy.fd

rem ビルド成果物ディレクトリ (buildフォルダ)
set BUILD_DIR=..\build

rem -----------------------------------------------------------

rem ファイル存在確認
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

rem 変数領域用ファイルを一時ファイルとしてコピー
copy /y "%OVMF_VARS_ORIG%" "%OVMF_VARS_TMP%" > nul

echo Starting Sylphia-OS in QEMU (MikanOS OVMF style)...

rem QEMU起動
rem -drive if=pflash... - フラッシュメモリとしてマウント
rem readonly=on - コード領域は書き込み不可にする
"%QEMU_PATH%" ^
-m 512M ^
-drive if=pflash,format=raw,readonly=on,file=%OVMF_CODE% ^
-drive if=pflash,format=raw,file=%OVMF_VARS_TMP% ^
-drive file=fat:rw:%BUILD_DIR%,format=raw,media=disk ^
-net none ^
-monitor stdio

del "%OVMF_VARS_TMP%"

endlocal
