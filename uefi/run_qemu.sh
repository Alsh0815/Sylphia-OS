#!/usr/bin/env bash
set -euo pipefail
qemu-system-x86_64 \
  -machine pc,accel=tcg \
  -cpu max \
  -m 2G \
  -monitor stdio \
  -drive if=pflash,format=raw,readonly=on,file=./../devenv/OVMF_CODE.fd \
  -drive if=pflash,format=raw,file=./../devenv/OVMF_VARS.fd \
  -drive format=raw,file=fat:rw:../vfat \
  -drive id=drv0,if=none,file=../nvme_disk.img,format=raw \
  -device nvme,drive=drv0,serial=deadbeef
