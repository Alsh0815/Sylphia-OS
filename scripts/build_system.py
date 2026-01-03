from colorama import Fore, Style, init
from enum import Enum, auto
from pathlib import Path
import click
import datetime
import json
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
    AARCH64 = auto()
    X86_64 = auto()
    RISCV64 = auto()

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
        self.build_scripts_dir = os.path.join(self.root_dir, "build_scripts")
        self.os_type = platform.system()

    def _get_arch_key(self, target: BuildTarget) -> str:
        """BuildTarget を config の辞書キーに変換"""
        mapping = {
            BuildTarget.AARCH64: "aarch64",
            BuildTarget.X86_64: "x86_64",
            BuildTarget.RISCV64: "riscv64",
        }
        return mapping.get(target, "x86_64")

    def _get_arch_config(self, target: BuildTarget) -> dict:
        """アーキテクチャ別設定を取得"""
        key = self._get_arch_key(target)
        return Config.ARCH_CONFIG.get(key, Config.ARCH_CONFIG["x86_64"])

    def __clean(self):
        if os.path.exists(self.build_dir):
            shutil.rmtree(self.build_dir)
        os.makedirs(self.build_dir)
    
    def _is_target_arch_file(self, file_path: str, target_arch: str) -> bool:
        """
        ファイルパスが現在のターゲットアーキテクチャに適しているか判定する
        archディレクトリ以下のファイルについて、target_archと異なるアーキテクチャディレクトリにある場合はFalseを返す
        """
        parts = file_path.split(os.sep)
        if "arch" in parts:
            try:
                arch_index = parts.index("arch")
                if arch_index + 1 < len(parts):
                    file_arch = parts[arch_index + 1]
                    # arch直下がアーキテクチャ名でない場合（共通コードなど）は対象とするが、
                    # 今回は arch/x86_64 のような構造を想定
                    # configにあるキーと一致するか確認すべきだが、簡易的に
                    # target_archと不一致なら除外とする
                    # ただし、ファイルアーキテクチャが既知のアーキテクチャディレクトリの場合のみチェック
                    known_archs = ["x86_64", "aarch64", "riscv64"]
                    if file_arch in known_archs and file_arch != target_arch:
                        return False
            except ValueError:
                pass
        return True

    def __build_app(self, app: str, pbar: tqdm, target: BuildTarget = BuildTarget.X86_64) -> list[str]:
        arch_config = self._get_arch_config(target)
        arch_key = self._get_arch_key(target)
        pbar.set_description(app)
        files = []
        ret_files = []
        for root, dirs, _files in os.walk(os.path.join(self.apps_dir, app)):
            for dir in dirs:
                os.makedirs(os.path.join(root, dir).replace(self.apps_dir, self.bin_apps_dir), exist_ok=True)
            for file in _files:
                if not self._is_target_arch_file(os.path.join(root, file), arch_key):
                    continue
                if file.endswith(".asm") or file.endswith(".s") or file.endswith(".cpp"):
                    files.append(os.path.join(root, file))
        os.makedirs(os.path.join(self.bin_apps_dir, app), exist_ok=True)
        for file in files:
            pbar.set_postfix_str(file)
            result = None
            _o_file = file.replace(self.apps_dir, self.bin_apps_dir)+".o"
            if file.endswith(".asm"):
                pbar.update(1)
                continue
            elif file.endswith(".s"):
                clang_cmd = [
                    Config.Compiler.CLANG_PATH,
                    "-target", arch_config["clang_target"],
                    "-c", str(file),
                    "-o", _o_file
                ]
                result = subprocess.run(
                    clang_cmd,
                    cwd=self.root_dir,
                    capture_output=True,
                    text=True
                )
            elif file.endswith(".cpp"):
                clang_cmd = [
                    Config.Compiler.CLANGXX_PATH,
                    "-target", arch_config["clang_target"],
                    "-ffreestanding", "-fno-rtti", "-fno-exceptions",
                    "-O2", "-Wall",
                ]
                clang_cmd.extend(arch_config["extra_cflags"])
                clang_cmd.extend(["-c", str(file), "-o", _o_file])
                result = subprocess.run(
                    clang_cmd,
                    cwd=self.root_dir,
                    capture_output=True,
                    text=True
                )
            ret_files.append(_o_file)
            checkResult(result, pbar, app)
            pbar.update(1)
        return ret_files
    
    def __build_bootloader(self, target: BuildTarget = BuildTarget.X86_64):
        # ... (unchanged) ...
        arch_config = self._get_arch_config(target)
        arch_key = self._get_arch_key(target)
        print(f"Building bootloader for {arch_key}...")
        os.makedirs(os.path.join(self.output_dir, "EFI", "BOOT"), exist_ok=True)
        os.makedirs(self.bin_bootloader_dir, exist_ok=True)
        target_files = []

        for root, dirs, files in os.walk(self.bootloader_src_dir):
            for dir in dirs:
                os.makedirs(os.path.join(root, dir).replace(self.bootloader_src_dir, self.bin_bootloader_dir), exist_ok=True)
            for file in files:
                if not self._is_target_arch_file(os.path.join(root, file), arch_key):
                    continue
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
                    clang_cmd = [
                        Config.Compiler.CLANG_PATH,
                        "-target", arch_config["clang_bootloader_target"],
                        "-fno-stack-protector", "-fshort-wchar",
                        "-c", str(file),
                        "-o", _obj
                    ]
                    # x86_64の場合のみ -mno-red-zone を追加
                    if target == BuildTarget.X86_64:
                        clang_cmd.insert(4, "-mno-red-zone")
                    result = subprocess.run(
                        clang_cmd,
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
            
            bootloader_output = arch_config["bootloader_output"]
            cmd = [
                Config.Linker.LLD_LINK_PATH,
                "/subsystem:efi_application",
                "/entry:EfiMain",
                "/dll",
                f"/out:{os.path.join(self.output_dir, 'EFI', 'BOOT', bootloader_output)}",
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
    
    def __build_kernel(self, target: BuildTarget = BuildTarget.X86_64, flags: list[str] = []):
        arch_config = self._get_arch_config(target)
        arch_key = self._get_arch_key(target)
        print(f"Building kernel for {arch_key}...")
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
                if not self._is_target_arch_file(os.path.join(root, file), arch_key):
                    continue
                if file.endswith(".asm") or file.endswith(".s"):
                    target_files.append(os.path.join(root, file))
                elif file.endswith(".cpp"):
                    target_files.append(os.path.join(root, file))
        for root, dirs, files in os.walk(self.std_dir):
            for file in files:
                if not self._is_target_arch_file(os.path.join(root, file), arch_key):
                    continue
                if file.endswith(".cpp"):
                    target_files.append(os.path.join(root, file))
        
        build_time = datetime.datetime.now()
        project_build_time = [build_time.year, build_time.month, build_time.day]
        project_version = [0, 0, 0, 0]
        with open(os.path.join(self.root_dir, "project.json"), "r") as f:
            project_json = json.load(f)
            project_version = project_json["project"]["version"]

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
                    pbar.update(1)
                    continue
                elif file.endswith(".s"):
                    # GAS構文アセンブリ（Clang内蔵アセンブラで処理）
                    pbar.set_postfix_str(file)
                    clang_cmd = [
                        Config.Compiler.CLANG_PATH,
                        "-target", arch_config["clang_target"],
                        "-c", str(file),
                        "-o", _obj
                    ]
                    result = subprocess.run(
                        clang_cmd,
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
                        "-target", arch_config["clang_target"],
                        "-ffreestanding", "-fno-rtti", "-fno-exceptions",
                        "-I.", f"-I{self.kernel_src_dir}", "-O2", "-Wall",
                        f"-DSYLPH_VERSION_MAJOR={project_version[0]}",
                        f"-DSYLPH_VERSION_MINOR={project_version[1]}",
                        f"-DSYLPH_VERSION_PATCH={project_version[2]}",
                        f"-DSYLPH_VERSION_REVISION={project_version[3]}",
                        f"-DSYLPH_BUILD_DATE_YEAR={project_build_time[0]}",
                        f"-DSYLPH_BUILD_DATE_MONTH={project_build_time[1]}",
                        f"-DSYLPH_BUILD_DATE_DAY={project_build_time[2]}"
                    ]
                    clang_cmd.extend(arch_config["extra_cflags"])
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
                    rust_target = arch_config["rust_target"]
                    result = subprocess.run(
                        [
                            Config.Compiler.CARGO_PATH,
                            "build",
                            "--release",
                            "--target", rust_target
                        ],
                        cwd=os.path.join(self.kernel_src_dir, "rust"),
                        capture_output=True,
                        text=True
                    )
                    o_files.append(os.path.join(self.kernel_src_dir, "rust", "target", rust_target, "release", "libsylphia_rust.a"))
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
                "-T", f"{os.path.join(self.kernel_src_dir, arch_config.get('linker_script', 'kernel.ld'))}",
                "--static",
            ]
            # AArch64ではPIEとしてビルドしてリロケーション情報を生成
            if target == BuildTarget.AARCH64:
                cmd.append("--pie")
            cmd.extend(["-o", f"{os.path.join(self.output_dir, 'kernel.elf')}"])
            cmd.extend(o_files)
            result = subprocess.run(
                cmd,
                cwd=self.root_dir,
                capture_output=True,
                text=True,
                shell=True
            )
            checkResult(result, pbar, "kernel")

    def build_app(self, target: BuildTarget = BuildTarget.X86_64, apps: list[str] = []):
        print("Building apps...")
        os.makedirs(os.path.join(self.output_dir, "apps"), exist_ok=True)
        if len(apps) == 0:
            for dirs in os.listdir(self.apps_dir):
                if dirs == "_header" or dirs == "_link":
                    continue
                apps.append(dirs)
        files = []
        for app in apps:
            for root, dirs, _files in os.walk(os.path.join(self.apps_dir, app)):
                for file in _files:
                    if file.endswith(".s"):
                        files.append(os.path.join(root, file))
                    elif file.endswith(".cpp"):
                        files.append(os.path.join(root, file))
        with tqdm(
            total=1,
            desc="_link",
            unit="file(s)",
            bar_format="{l_bar}{bar}| {n_fmt}/{total_fmt} [{elapsed}<{remaining}]"
        ) as pbar:
            self.__build_app("_link", pbar, target)
        with tqdm(
            total=len(files),
            desc="apps",
            unit="file(s)",
            bar_format="{l_bar}{bar}| {n_fmt}/{total_fmt} [{elapsed}<{remaining}]"
        ) as pbar:
            for app in apps:
                files = self.__build_app(app, pbar, target)
                cmd = [
                    Config.Linker.LD_LLD_PATH,
                    "-T", f"{os.path.join(self.apps_dir, '_link', 'linker.ld')}",
                    "-o", f"{os.path.join(self.output_dir, 'apps', app + '.elf')}",
                    f"{os.path.join(self.bin_apps_dir, '_link', 'arch', 'x86_64' if target == BuildTarget.X86_64 else 'aarch64', 'start.s.o')}"
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
        target: BuildTarget = BuildTarget.X86_64,
        package: BuildPackage = [BuildPackage.APP, BuildPackage.BOOTLOADER, BuildPackage.KERNEL],
        flags: list[str] = []
    ):
        for p in package:
            if p == BuildPackage.APP:
                self.build_app(target = target)
            elif p == BuildPackage.BOOTLOADER:
                self.__build_bootloader(target = target)
            elif p == BuildPackage.KERNEL:
                self.__build_kernel(target = target, flags = flags)