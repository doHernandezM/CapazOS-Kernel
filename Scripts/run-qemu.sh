#!/usr/bin/env bash
#
# Launch the combined boot+kernel image under QEMU.
#
# This script executes qemu-system-aarch64 in a configuration suitable for
# testing the boot stage.  It uses the `virt` machine, the Cortex‑A72
# compatible CPU, 128 MiB of RAM and maps the combined image at
# physical address 0x4000_0000.  The serial console is redirected to
# the host standard input and output.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
IMAGE="$ROOT/build/kernel.img"

if [[ ! -f "$IMAGE" ]]; then
  echo "Image $IMAGE not found.  Did you run build.sh?" >&2
  exit 1
fi

DEBUG_PORT="${DEBUG_PORT:-}" # set DEBUG_PORT=1234 to enable gdb

exec qemu-system-aarch64 \
  -machine virt,gic-version=2 \
  -cpu cortex-a72 \
  -m 128M \
  -nographic \
  -serial mon:stdio \
  -kernel "$IMAGE" \
  -no-reboot -no-shutdown \
  ${DEBUG_PORT:+-S -gdb tcp::${DEBUG_PORT}}
