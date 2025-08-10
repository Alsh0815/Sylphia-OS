#!/usr/bin/env bash
set -euo pipefail
qemu-system-x86_64 \
  -machine q35,accel=tcg \
  -cpu max \
  -m 256M \
  -drive if=pflash,format=raw,readonly=on,file=./../devenv/OVMF_CODE.fd \
  -drive if=pflash,format=raw,file=./../devenv/OVMF_VARS.fd \
  -drive format=raw,file=fat:rw:../vfat
