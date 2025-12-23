#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
ELF="$ROOT/build/Kernel.elf"

export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"

echo "Kernel localtion:"
echo $ELF

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
