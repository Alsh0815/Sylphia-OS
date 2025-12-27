import subprocess
from colorama import Fore, Style, init
from enum import Enum, auto
from pathlib import Path
import click
import os
import platform
import shutil
import subprocess
import sys

from tqdm import tqdm

from . import config as Config


def checkResult(result: subprocess.CompletedProcess, pbar: tqdm, app: str):
    if result and result.returncode != 0:
        pbar.close()
        click.echo(Fore.RED + f"\n\n[ERROR] Build failed in: {app}")
        click.echo(Style.BRIGHT + "Command output:")
        click.echo(result.stdout)
        click.echo(Fore.YELLOW + result.stderr)
        sys.exit(1)


class BuildPackage(Enum):
    APP = auto()
    BOOTLOADER = auto()
    KERNEL = auto()

class BuildTarget(Enum):
    X86_64 = auto()

class SylphiaBuildSystem:
    def __init__(self):
        self.root_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
        self.build_dir = os.path.join(self.root_dir, "build")
        self.bin_dir = os.path.join(self.build_dir, "bin")
        self.bin_apps_dir = os.path.join(self.bin_dir, "apps")
        self.bin_bootloader_dir = os.path.join(self.bin_dir, "bootloader")
        self.bin_kernel_dir = os.path.join(self.bin_dir, "kernel")
        self.output_dir = os.path.join(self.build_dir, "output")
        self.apps_dir = os.path.join(self.root_dir, "apps")
        self.bootloader_src_dir = os.path.join(self.root_dir, "bootloader")
        self.kernel_src_dir = os.path.join(self.root_dir, "kernel")
        self.std_dir = os.path.join(self.root_dir, "std")
        self.os_type = platform.system()

    def __clean(self):
        if os.path.exists(self.build_dir):
            shutil.rmtree(self.build_dir)
        os.makedirs(self.build_dir)
    
    def __build_app(self, app: str, pbar: tqdm) -> list[str]:
        pbar.set_description(app)
        files = []
        ret_files = []
        for root, dirs, _files in os.walk(os.path.join(self.apps_dir, app)):
            for file in _files:
                if file.endswith(".asm") or file.endswith(".cpp"):
                    files.append(os.path.join(root, file))
        os.makedirs(os.path.join(self.bin_apps_dir, app), exist_ok=True)
        for file in files:
            pbar.set_postfix_str(file)
            result = 0
            _o_file = file.replace(self.apps_dir, self.bin_apps_dir)+".o"
            if file.endswith(".asm"):
                result = subprocess.run(
                    [
                        Config.Compiler.NASM_PATH,
                        "-f", "elf64",
                        str(file),
                        "-o", _o_file
                    ],
                    cwd=self.root_dir,
                    capture_output=True,
                    text=True
                )
            elif file.endswith(".cpp"):
                result = subprocess.run(
                    [
                        Config.Compiler.CLANGXX_PATH,
                        "-target", "x86_64-pc-none-elf",
                        "-ffreestanding", "-fno-rtti", "-fno-exceptions",
                        "-O2", "-Wall",
                        "-c", str(file),
                        "-o", _o_file
                    ],
                    cwd=self.root_dir,
                    capture_output=True,
                    text=True
                )
            ret_files.append(_o_file)
            checkResult(result, pbar, app)
            pbar.update(1)
        return ret_files
    
    def __build_bootloader(self):
        print("Building bootloader...")
        os.makedirs(os.path.join(self.output_dir, "EFI", "BOOT"), exist_ok=True)
        os.makedirs(self.bin_bootloader_dir, exist_ok=True)
        target_files = []

        for root, dirs, files in os.walk(self.bootloader_src_dir):
            for dir in dirs:
                os.makedirs(os.path.join(root, dir).replace(self.bootloader_src_dir, self.bin_bootloader_dir), exist_ok=True)
            for file in files:
                if file.endswith(".c"):
                    target_files.append(os.path.join(root, file))
        
        o_files = []
        with tqdm(
            total=len(target_files),
            desc="bootloader",
            unit="file(s)",
            bar_format="{l_bar}{bar}| {n_fmt}/{total_fmt} [{elapsed}<{remaining}]"
        ) as pbar:
            for file in target_files:
                _obj = file.replace(self.bootloader_src_dir, self.bin_bootloader_dir)+".o"
                if file.endswith(".c"):
                    pbar.set_postfix_str(file)
                    result = subprocess.run(
                        [
                            Config.Compiler.CLANG_PATH,
                            "-target", "x86_64-pc-win32-coff",
                            "-fno-stack-protector", "-fshort-wchar", "-mno-red-zone",
                            "-c", str(file),
                            "-o", _obj
                        ],
                        cwd=self.root_dir,
                        capture_output=True,
                        text=True
                    )
                    o_files.append(_obj)
                    pbar.update(1)
                else:
                    pbar.set_postfix_str(file)
                    pbar.update(1)
                    pass
                checkResult(result, pbar, "bootloader")
            cmd = [
                Config.Linker.LLD_LINK_PATH,
                "/subsystem:efi_application",
                "/entry:EfiMain",
                "/dll",
                f"/out:{os.path.join(self.output_dir, "EFI", "BOOT", "BOOTX64.EFI")}",
            ]
            cmd.extend(o_files)
            result = subprocess.run(
                cmd,
                cwd=self.root_dir,
                capture_output=True,
                text=True,
                shell=True
            )
            checkResult(result, pbar, "bootloader")
    
    def __build_kernel(self, flags: list[str] = []):
        print("Building kernel...")
        os.makedirs(self.output_dir, exist_ok=True)
        os.makedirs(self.bin_kernel_dir, exist_ok=True)
        target_files = []

        for root, dirs, files in os.walk(self.kernel_src_dir):
            for dir in dirs:
                if dir == "rust":
                    target_files.append(os.path.join(root, dir))
                    continue
                os.makedirs(os.path.join(root, dir).replace(self.kernel_src_dir, self.bin_kernel_dir), exist_ok=True)
            for file in files:
                if file.endswith(".asm"):
                    target_files.append(os.path.join(root, file))
                elif file.endswith(".cpp"):
                    target_files.append(os.path.join(root, file))
        for root, dirs, files in os.walk(self.std_dir):
            for file in files:
                if file.endswith(".cpp"):
                    target_files.append(os.path.join(root, file))

        o_files = []
        with tqdm(
            total=len(target_files),
            desc="kernel",
            unit="file(s)",
            bar_format="{l_bar}{bar}| {n_fmt}/{total_fmt} [{elapsed}<{remaining}]"
        ) as pbar:
            for file in target_files:
                _obj = file.replace(self.kernel_src_dir, self.bin_kernel_dir)+".obj"
                result = None
                if file.endswith(".asm"):
                    pbar.set_postfix_str(file)
                    result = subprocess.run(
                        [
                            Config.Compiler.NASM_PATH,
                            "-f", "elf64",
                            str(file),
                            "-o", _obj
                        ],
                        cwd=self.root_dir,
                        capture_output=True,
                        text=True
                    )
                    o_files.append(_obj)
                    pbar.update(1)
                elif file.endswith(".cpp"):
                    pbar.set_postfix_str(file)
                    clang_cmd = [
                        Config.Compiler.CLANGXX_PATH,
                        "-target", "x86_64-elf",
                        "-ffreestanding", "-fno-rtti", "-fno-exceptions",
                        "-mno-red-zone", "-mgeneral-regs-only",
                        "-I.", f"-I{self.kernel_src_dir}", "-O2", "-Wall",
                    ]
                    for flag in flags:
                        clang_cmd.append(f"-D{flag}")
                    clang_cmd.extend(["-c", str(file), "-o", _obj])
                    result = subprocess.run(
                        clang_cmd,
                        cwd=self.root_dir,
                        capture_output=True,
                        text=True
                    )
                    o_files.append(_obj)
                    pbar.update(1)
                elif file.endswith("rust"):
                    result = subprocess.run(
                        [
                            Config.Compiler.CARGO_PATH,
                            "build",
                            "--release",
                            "--target", "x86_64-unknown-none"
                        ],
                        cwd=os.path.join(self.kernel_src_dir, "rust"),
                        capture_output=True,
                        text=True
                    )
                    o_files.append(os.path.join(self.kernel_src_dir, "rust", "target", "x86_64-unknown-none", "release", "libsylphia_rust.a"))
                    pbar.update(1)
                    pass
                else:
                    pbar.set_postfix_str(file)
                    pbar.update(1)
                    pass
                checkResult(result, pbar, "kernel")
            cmd = [
                Config.Linker.LD_LLD_PATH,
                "-entry", "KernelMain",
                "-z", "norelro",
                "-T", f"{os.path.join(self.kernel_src_dir, 'kernel.ld')}",
                "--static",
                "-o", f"{os.path.join(self.output_dir, 'kernel.elf')}",
            ]
            cmd.extend(o_files)
            result = subprocess.run(
                cmd,
                cwd=self.root_dir,
                capture_output=True,
                text=True,
                shell=True
            )
            checkResult(result, pbar, "kernel")

    def build_app(self, target: BuildTarget, apps: list[str] = []):
        print("Building apps...")
        os.makedirs(os.path.join(self.output_dir, "apps"), exist_ok=True)
        if len(apps) == 0:
            for root, dirs, _files in os.walk(self.apps_dir):
                for dir in dirs:
                    if dir == "_header" or dir == "_link":
                        continue
                    apps.append(dir)
        files = []
        for app in apps:
            for root, dirs, _files in os.walk(os.path.join(self.apps_dir, app)):
                for file in _files:
                    if file.endswith(".asm"):
                        files.append(os.path.join(root, file))
                    elif file.endswith(".cpp"):
                        files.append(os.path.join(root, file))
        with tqdm(
            total=1,
            desc="_link",
            unit="file(s)",
            bar_format="{l_bar}{bar}| {n_fmt}/{total_fmt} [{elapsed}<{remaining}]"
        ) as pbar:
            self.__build_app("_link", pbar)
        with tqdm(
            total=len(files),
            desc="apps",
            unit="file(s)",
            bar_format="{l_bar}{bar}| {n_fmt}/{total_fmt} [{elapsed}<{remaining}]"
        ) as pbar:
            for app in apps:
                files = self.__build_app(app, pbar)
                cmd = [
                    Config.Linker.LD_LLD_PATH,
                    "-T", f"{os.path.join(self.apps_dir, '_link', 'linker.ld')}",
                    "-o", f"{os.path.join(self.output_dir, 'apps', app + '.elf')}",
                    f"{os.path.join(self.bin_apps_dir, '_link', 'start.asm.o')}"
                ]
                cmd.extend(files)
                cmd.extend(["-entry", "_start"])
                result = subprocess.run(
                    cmd,
                    cwd=self.root_dir,
                    capture_output=True,
                    text=True,
                    shell=True
                )
                checkResult(result, pbar, app)

    def build(
        self,
        target: BuildTarget,
        package: BuildPackage = [BuildPackage.APP, BuildPackage.BOOTLOADER, BuildPackage.KERNEL],
        flags: list[str] = []
    ):
        for p in package:
            if p == BuildPackage.APP:
                self.build_app(target = target)
            elif p == BuildPackage.BOOTLOADER:
                self.__build_bootloader()
            elif p == BuildPackage.KERNEL:
                self.__build_kernel(flags)