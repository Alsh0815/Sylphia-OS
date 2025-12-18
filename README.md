# Sylphia-OS

Sylphia-OSは、フリースタンディング環境で動作する64ビット自作オペレーティングシステムです。
C++ベースで実装されており、UEFIブート、xHCI (USB 3.0)、NVMeドライバーなどを備えています。

## 利用方針と免責事項

本OSは**完全なオープンソース**であり、**使用目的を一切制限しません**。

- **自由な実験**: カーネル破壊の実験、マルウェア解析のサンドボックス、セキュリティ検証のターゲットなど、どのような目的でもご利用いただけます。
- **改変・配布**: [MIT License](LICENSE.md) の下、自由に改変・再配布・商用利用が可能です。

**【免責事項】**
本ソフトウェアの使用によって生じた、いかなる損害（データ消失、ハードウェア破損、セキュリティインシデント等）について、開発者は一切の責任を負いません。全て**自己責任**でご利用ください。利用者は、本ソフトウェアを実行した時点でこの条件に同意したものとみなされます。詳細は[ライセンス](LICENSE.md)を参照してください。

## 必要要件

Windows環境でのビルドを想定しています。以下のツールにPATHを通してください。

- **Clang / LLVM** (`clang++`, `ld.lld`)
- **NASM** (`nasm`)
- **PowerShell**

エミュレータを用いて起動するには以下のソフトウェアが必要です。

- **QEMU** (qemu-system-x86_64)

## ビルド手順

1. リポジトリをクローンします。

    ```powershell
    git clone https://github.com/Alsh0815/Sylphia-OS.git
    cd Sylphia-OS
    ```

2. ビルドスクリプトを実行します。

    ```powershell
    cd build_scripts
    .\build_bootloader.ps1
    .\build_kernel.ps1
    ```

    ※カーネルの動作にはプリインストールアプリのビルドが必要です。`.\build_app.ps1 <APP_NAME>`を実行してください。

## QEMUでの実行方法

```powershell
cd build_scripts
.\run_qemu.ps1
```

## ライセンス

[MIT License](LICENSE.md)
