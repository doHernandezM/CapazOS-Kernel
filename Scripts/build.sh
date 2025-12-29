#!/usr/bin/env bash
#
# Build script for the Capaz boot‑only project.
#
# This script produces two separate binaries: a minimal boot image
# (boot.elf) that runs in identity‑mapped memory and a stub kernel
# image (kernel.elf) that will be fleshed out later.  The two are
# concatenated into a flat image (kernel.img) padded to a 2 MiB
# boundary.  On macOS the cross‑compiler must be installed via
# Homebrew (see toolchain.env).

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
KERNEL_DIR="$ROOT/Kernel"
OUT_DIR="$ROOT/build"

mkdir -p "$OUT_DIR/obj"

# Load toolchain definitions
source "$KERNEL_DIR/Scripts/toolchain.env"

# Common compiler flags for both boot and kernel objects
COMMON_CFLAGS=(
  -ffreestanding -fno-builtin -fno-stack-protector
  -fno-pic -fno-pie
  -Wall -Wextra
  -mcpu=cortex-a72
  -target aarch64-none-none-elf
  -g -O0
  -I"$KERNEL_DIR/Sources/HAL"
)

BOOT_CFLAGS=("${COMMON_CFLAGS[@]}" -DBOOT_STAGE=1)
KERNEL_CFLAGS=("${COMMON_CFLAGS[@]}")

################################################################################
# Build the boot image
################################################################################

# Compile the boot assembly.  start.S contains the _start symbol and a
# minimal UART driver implemented in assembly.  No C code is used for
# the boot image at this stage.
"$CC" "${BOOT_CFLAGS[@]}" -c "$KERNEL_DIR/Sources/Arch/aarch64/start.S" -o "$OUT_DIR/obj/start.o"

# Link the boot image
"$LD" -nostdlib -T "$KERNEL_DIR/Linker/boot.ld" \
  "$OUT_DIR/obj/start.o" \
  -o "$OUT_DIR/boot.elf"

# Optionally produce a flat binary of the boot image
"$OBJCOPY" -O binary "$OUT_DIR/boot.elf" "$OUT_DIR/boot.bin"

################################################################################
# Build the kernel image (stub)
################################################################################

# Compile the kernel stub and the UART driver.  In future milestones
# additional files will be added here.
"$CC" "${KERNEL_CFLAGS[@]}" -c "$KERNEL_DIR/Sources/Kernel/kmain.c" -o "$OUT_DIR/obj/kmain.o"
"$CC" "${KERNEL_CFLAGS[@]}" -c "$KERNEL_DIR/Sources/HAL/uart_pl011.c" -o "$OUT_DIR/obj/uart.o"

# Link the kernel image
"$LD" -nostdlib -T "$KERNEL_DIR/Linker/kernel.ld" \
  "$OUT_DIR/obj/kmain.o" \
  "$OUT_DIR/obj/uart.o" \
  -o "$OUT_DIR/kernel.elf"

# Optionally produce a flat binary of the kernel image
"$OBJCOPY" -O binary "$OUT_DIR/kernel.elf" "$OUT_DIR/kernel.bin"

################################################################################
# Combine boot and kernel into a single flat image padded to 2 MiB
################################################################################

BOOT_BIN="$OUT_DIR/boot.bin"
KERNEL_BIN="$OUT_DIR/kernel.bin"
COMBINED_IMG="$OUT_DIR/kernel.img"

if [[ -f "$BOOT_BIN" && -f "$KERNEL_BIN" ]]; then
  # Determine boot binary size.  Use GNU stat if available, otherwise fall back
  # to BSD stat.  This allows the script to be portable across Linux and
  # macOS.
  if BOOT_SIZE=$(stat -c%s "$BOOT_BIN" 2>/dev/null); then
    :
  else
    BOOT_SIZE=$(stat -f%z "$BOOT_BIN")
  fi
  ALIGN=2097152 # 2 MiB alignment
  PAD_SIZE=$(( (ALIGN - (BOOT_SIZE % ALIGN)) % ALIGN ))
  BOOT_PAD="$OUT_DIR/boot.padded.bin"
  cp "$BOOT_BIN" "$BOOT_PAD"
  if [[ $PAD_SIZE -ne 0 ]]; then
    dd if=/dev/zero bs=1 count=$PAD_SIZE >> "$BOOT_PAD" 2>/dev/null
  fi
  cat "$BOOT_PAD" "$KERNEL_BIN" > "$COMBINED_IMG"
  rm -f "$BOOT_PAD"
fi

echo "Built boot and kernel images."