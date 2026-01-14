#!/usr/bin/env bash
set -euo pipefail

# Platform defaults for QEMU aarch64 'virt' machine.

BOOT_LOAD_ADDR=$((0x40080000))
RAM_BASE=$((0x40000000))
HH_PHYS_4000_BASE="0xFFFF800040000000"
ALIGN=$((2097152)) # 2 MiB

# QEMU defaults
QEMU_MACHINE="virt"
QEMU_CPU="cortex-a72"
QEMU_MEM="128M"
QEMU_EXTRA_ARGS=(
  -nographic
  -serial mon:stdio
)

# Default debug port (override by exporting GDB_PORT)
GDB_PORT="${GDB_PORT:-1234}"
