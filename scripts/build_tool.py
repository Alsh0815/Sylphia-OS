import click
from colorama import Fore, Style, init
import click
import os
import platform
import subprocess

from . import build_system as _build
from . import run as _run


init(autoreset=True)


def str2bt(target: str) -> _build.BuildTarget:
    if target == 'aarch64' or target == 'arm64':
        return _build.BuildTarget.AARCH64
    elif target == 'x86_64' or target == 'x64':
        return _build.BuildTarget.X86_64
    elif target == 'riscv64' or target == 'riscv':
        return _build.BuildTarget.RISCV64
    else:
        raise ValueError(f"Unknown target: {target}. Supported: x86_64, aarch64, riscv64")

def str2pkg(package: str) -> _build.BuildPackage:
    if package == 'app':
        return _build.BuildPackage.APP
    elif package == 'bootloader':
        return _build.BuildPackage.BOOTLOADER
    elif package == 'kernel':
        return _build.BuildPackage.KERNEL
    else:
        raise ValueError(f"Unknown package: {package}")


ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))


@click.group()
def main():
    pass


@main.command()
@click.option('--name', '-n', default='', help='Target package name (ex. APP_NAME_1,APP_NAME_2, ...)')
@click.option('--package', '-p', default='', help='Build package [app,bootloader,kernel]')
@click.option('--target', '-t', default='x86_64', help='Target architecture')
@click.option('--flags', '-f', default='', help='Compiler flags (ex. DEBUG_BUILD,FEATURE_X)')
def build(name, package, target, flags):
    build_system = _build.SylphiaBuildSystem()
    flag_list = [f for f in flags.split(',') if f] if flags else []
    if 'DEBUG_BUILD' in flag_list:
        print("\033[7m  FLAG: DEBUG_BUILD  \033[m")
    if not name == '' and package == 'app':
        build_system.build_app(str2bt(target), name.split(','))
    elif package == '':
        build_system.build(str2bt(target), [_build.BuildPackage.APP, _build.BuildPackage.BOOTLOADER, _build.BuildPackage.KERNEL], flag_list)
    else:
        build_system.build(str2bt(target), [str2pkg(p) for p in package.split(',')], flag_list)


@main.command()
def clean():
    if platform.system() == 'Windows':
        subprocess.run(f"rmdir /s /q \"{os.path.join(ROOT, "build")}\"", shell=True, check=True)
    elif platform.system() == 'Linux':
        subprocess.run(f"rm -rf \"{os.path.join(ROOT, "build")}\"", shell=True, check=True)


@main.command()
def dump():
    if platform.system() == 'Windows':
        subprocess.run(f"llvm-objdump -d -S -C \"{os.path.join(ROOT, "build", "output", "kernel.elf")}\" > \"{os.path.join(ROOT, "build", "kernel_dump.txt")}\"", shell=True, check=True)
    elif platform.system() == 'Linux':
        subprocess.run(f"objdump -d -S -C \"{os.path.join(ROOT, "build", "output", "kernel.elf")}\" > \"{os.path.join(ROOT, "build", "kernel_dump.txt")}\"", shell=True, check=True)


@main.command()
@click.option('--clean', is_flag=True, default=False, help='Boot Sylphia-OS in clean install mode.')
@click.option('--target', '-t', default='x86_64', help='Target architecture')
def run(clean, target):
    _run.run(clean, target)


@main.command()
@click.option('--clean', is_flag=True, default=False, help='Boot Sylphia-OS in clean install mode.')
@click.option('--target', '-t', default='x86_64', help='Target architecture')
@click.option('--timeout', default=30, help='Timeout in seconds')
def test(clean, target, timeout):
    _run.run(clean, target, test=True, timeout=timeout)


if __name__ == "__main__":
    main()