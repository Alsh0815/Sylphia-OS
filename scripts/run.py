import os
import platform
import shutil
import subprocess

from . import config as Config
from . import img as Img


ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))


def run(clean: bool, target: str, test: bool = False, timeout: int = 30):
    executor = Config.QEMU.QEMU_X64_PATH
    if target == 'arm':
        executor = Config.QEMU.QEMU_ARM_PATH
    elif target == 'arm64' or target == 'aarch64':
        executor = Config.QEMU.QEMU_ARM64_PATH
    elif target == 'i386' or target == 'x86':
        executor = Config.QEMU.QEMU_I386_PATH
    elif target == 'x86_64' or target == 'x64':
        executor = Config.QEMU.QEMU_X64_PATH
    else:
        raise ValueError(f"Unknown target: {target}")
    
    shutil.copyfile(os.path.join(ROOT, 'build_scripts', 'OVMF_VARS.fd'), os.path.join(ROOT, 'build_scripts', 'OVMF_VARS_TMP.fd'))

    if clean:
        if os.path.exists(os.path.join(ROOT, 'usb.vhd')):
            os.remove(os.path.join(ROOT, 'usb.vhd'))
        Img.copy_items_to_vhd()
        if os.path.exists(os.path.join(ROOT, 'nvme.img')):
            os.remove(os.path.join(ROOT, 'nvme.img'))
        if platform.system() == 'Windows':
            subprocess.run(f"fsutil file createnew \"{os.path.join(ROOT, 'nvme.img')}\" {Config.NVME_IMG_SIZE}")
        else:
            subprocess.run(f"dd if=/dev/zero of=\"{os.path.join(ROOT, 'nvme.img')}\" bs=1M count={Config.NVME_IMG_SIZE // (1024 * 1024)}")
    
    cmd = [executor, '-m', Config.QEMU_MEMORY]
    cmd.append('-drive')
    cmd.append(f"if=pflash,format=raw,readonly=on,file={os.path.join(ROOT, 'build_scripts', 'OVMF_CODE.fd')}")
    cmd.append('-drive')
    cmd.append(f"if=pflash,format=raw,file={os.path.join(ROOT, 'build_scripts', 'OVMF_VARS_TMP.fd')}")
    cmd.append('-drive')
    cmd.append(f"file={os.path.join(ROOT, 'nvme.img')},if=none,id=nvm")
    cmd.append('-device')
    cmd.append('nvme,serial=deadbeef,drive=nvm')
    cmd.append('-device')
    cmd.append('qemu-xhci,id=xhci')
    cmd.append('-device')
    cmd.append('usb-kbd')
    cmd.append('-net')
    cmd.append('none')
    cmd.append('-d')
    cmd.append('int')
    cmd.append('-D')
    cmd.append(f"{os.path.join(ROOT, 'build', 'qemu.log')}")

    if clean:
        cmd.append('-drive')
        cmd.append(f"file={os.path.join(ROOT, 'usb.vhd')},format=vpc,if=none,id=usbstick")
        cmd.append('-device')
        cmd.append('usb-storage,bus=xhci.0,drive=usbstick,bootindex=1')
    
    result = None
    if not test:    # normal mode
        cmd.append('-monitor')
        cmd.append('stdio')
        result = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, shell=True, encoding='utf-8')
    else:           # test mode
        cmd.append('-serial')
        cmd.append('stdio')

        try:
            print("[QEMU] Starting...")
            result = subprocess.run(cmd, cwd=ROOT, shell=True, encoding='utf-8')
        except subprocess.TimeoutExpired:
            print("[QEMU] Timed out")
            return

    if result.returncode != 0:
        print(result.stderr)
        return