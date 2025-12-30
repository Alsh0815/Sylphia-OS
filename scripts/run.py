import os
import platform
import shutil
import subprocess
import sys
import threading
import time

from . import config as Config
from . import img as Img


ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))


def run_interactive(cmd_args, timeout=300):
    proc = subprocess.Popen(
        cmd_args,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        shell=False,
        bufsize=1
    )

    def output_reader():
        try:
            while True:
                char = proc.stdout.read(1)
                if not char:
                    break
                sys.stdout.write(char)
                sys.stdout.flush()
        except Exception:
            pass

    reader_thread = threading.Thread(target=output_reader, daemon=True)
    reader_thread.start()

    print(f"\n\033[7m[System] Sylphia-OS Started. Type commands. Type '$exit' to stop.\033[m")

    try:
        while proc.poll() is None:
            cmd = input().strip()

            if cmd == "$exit":
                print("[System] Terminating Sylphia-OS...")
                break
            if proc.stdin:
                for char in (cmd + "\n"):
                    proc.stdin.write(char)
                    proc.stdin.flush()
                    time.sleep(0.02)

    except (EOFError, KeyboardInterrupt):
        pass
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
        print("[System] Sylphia-OS Halted.")

def run(clean: bool, target: str, test: bool = False, timeout: int = 30):
    # ターゲット文字列を正規化
    if target == 'arm64':
        target = 'aarch64'
    elif target == 'x64':
        target = 'x86_64'
    elif target == 'riscv':
        target = 'riscv64'
    
    # アーキテクチャ設定を取得
    if target not in Config.ARCH_CONFIG:
        raise ValueError(f"Unknown target: {target}. Supported: x86_64, aarch64, riscv64")
    
    arch_config = Config.ARCH_CONFIG[target]
    executor = arch_config["qemu_path"]
    
    # ファームウェアパス
    arch_dir = os.path.join(ROOT, 'build_scripts', 'arch')
    ovmf_code_path = os.path.join(arch_dir, arch_config["ovmf_code"])
    ovmf_vars_path = os.path.join(arch_dir, arch_config["ovmf_vars"]) if arch_config["ovmf_vars"] else None
    
    # ファームウェアの存在確認
    if not os.path.exists(ovmf_code_path):
        print(f"[ERROR] OVMF firmware not found: {ovmf_code_path}")
        print(f"        Please download the appropriate firmware for {target}.")
        return
    
    # OVMF_VARSの一時ファイルを作成（ある場合のみ）
    ovmf_vars_tmp_path = None
    if ovmf_vars_path and os.path.exists(ovmf_vars_path):
        ovmf_vars_tmp_path = os.path.join(ROOT, 'build_scripts', 'OVMF_VARS_TMP.fd')
        shutil.copyfile(ovmf_vars_path, ovmf_vars_tmp_path)

    if clean:
        if os.path.exists(os.path.join(ROOT, 'usb.vhd')):
            os.remove(os.path.join(ROOT, 'usb.vhd'))
        Img.copy_items_to_vhd()
        if os.path.exists(os.path.join(ROOT, 'nvme.img')):
            os.remove(os.path.join(ROOT, 'nvme.img'))
        if platform.system() == 'Windows':
            subprocess.run(f"fsutil file createnew \"{os.path.join(ROOT, 'nvme.img')}\" {Config.NVME_IMG_SIZE}")
        else:
            subprocess.run(f"dd if=/dev/zero of=\"{os.path.join(ROOT, 'nvme.img')}\" bs=1M count={Config.NVME_IMG_SIZE // (1024 * 1024)}", shell=True)
    
    cmd = [executor, '-m', Config.QEMU_MEMORY]
    
    # アーキテクチャ固有のマシン/CPU設定
    if arch_config["qemu_machine"]:
        cmd.extend(['-machine', arch_config["qemu_machine"]])
    if arch_config["qemu_cpu"]:
        cmd.extend(['-cpu', arch_config["qemu_cpu"]])
    
    # ファームウェア設定
    if target == 'x86_64':
        # x86_64: pflashを使用
        cmd.extend(['-drive', f"if=pflash,format=raw,readonly=on,file={ovmf_code_path}"])
        if ovmf_vars_tmp_path:
            cmd.extend(['-drive', f"if=pflash,format=raw,file={ovmf_vars_tmp_path}"])
    elif target == 'aarch64':
        # AArch64: pflashを使用
        cmd.extend(['-drive', f"if=pflash,format=raw,readonly=on,file={ovmf_code_path}"])
        if ovmf_vars_tmp_path:
            cmd.extend(['-drive', f"if=pflash,format=raw,file={ovmf_vars_tmp_path}"])
    elif target == 'riscv64':
        # RISC-V: biosオプションを使用
        cmd.extend(['-bios', ovmf_code_path])
    
    # 共通ストレージ設定
    cmd.extend(['-drive', f"file={os.path.join(ROOT, 'nvme.img')},if=none,id=nvm"])
    cmd.extend(['-device', 'nvme,serial=deadbeef,drive=nvm'])
    
    # USB/キーボード設定（アーキテクチャによって異なる場合あり）
    if target == 'x86_64':
        cmd.extend(['-device', 'qemu-xhci,id=xhci'])
        cmd.extend(['-device', 'usb-kbd'])
    elif target == 'aarch64' or target == 'riscv64':
        cmd.extend(['-device', 'ramfb'])
        cmd.extend(['-device', 'qemu-xhci,id=xhci'])
        cmd.extend(['-device', 'usb-kbd'])
    
    cmd.extend(['-net', 'none'])
    cmd.extend(['-d', 'int'])
    cmd.extend(['-D', f"{os.path.join(ROOT, 'build', 'qemu.log')}"])

    if clean:
        cmd.extend(['-drive', f"file={os.path.join(ROOT, 'usb.vhd')},format=vpc,if=none,id=usbstick"])
        cmd.extend(['-device', 'usb-storage,bus=xhci.0,drive=usbstick,bootindex=1'])
    
    result = None
    if not test:    # normal mode
        cmd.extend(['-monitor', 'stdio'])
        result = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, shell=True, encoding='utf-8')
        if result.returncode != 0:
            print(result.stderr)
            return
    else:           # test mode
        cmd.extend(['-serial', 'stdio'])

        try:
            print(f"[QEMU] Starting {target}...")
            run_interactive(cmd)
        except subprocess.TimeoutExpired:
            print("[QEMU] Timed out")
            return