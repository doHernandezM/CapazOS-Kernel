#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
# The unified build previously produced a single Kernel.elf.  With
# separate boot and kernel images the build script now emits a
# combined flat image (kernel.img) that contains the boot and kernel
# concatenated with appropriate padding.  Use this image as the
# -kernel argument to QEMU.  If you want to debug individual
# components you can still point -kernel at build/boot.elf or
# build/kernel.elf, but the default combined image reflects the new
# packaging.
ELF="$ROOT/build/kernel.img"

export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"

echo "Kernel image location:"
echo "$ELF"

DEBUG_PORT="${DEBUG_PORT:-1225}"

exec qemu-system-aarch64 \
  -machine virt \
  -cpu cortex-a72 \
  -m 128M \
  -nographic \
  -serial mon:stdio \
  -kernel "$ELF" \
  -no-reboot -no-shutdown \
  ${DEBUG:+-S} \
  -gdb "tcp::${DEBUG_PORT}"
