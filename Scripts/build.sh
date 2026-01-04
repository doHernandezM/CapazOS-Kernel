#!/usr/bin/env bash
# Kernel/Scripts/build.sh
#
# Boot + kernel two-image build. QEMU virt loads -kernel at 0x40080000,
# so KERNEL_PHYS_BASE is computed relative to that load address.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
KERNEL_DIR="$ROOT/Kernel"
OUT_DIR="$ROOT/build"

mkdir -p "$OUT_DIR/obj"

source "$KERNEL_DIR/Scripts/toolchain.env"

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

BOOT_LOAD_ADDR=$((0x40080000))
RAM_BASE=$((0x40000000))
HH_PHYS_4000_BASE="0xFFFF800040000000"
ALIGN=$((2097152)) # 2 MiB

################################################################################
# Build boot
################################################################################
"$CC" "${BOOT_CFLAGS[@]}" -c "$KERNEL_DIR/Sources/Arch/aarch64/start.S" -o "$OUT_DIR/obj/start.o"
"$LD" -nostdlib -T "$KERNEL_DIR/Linker/boot.ld" \
  "$OUT_DIR/obj/start.o" \
  -o "$OUT_DIR/boot.elf"
"$OBJCOPY" -O binary "$OUT_DIR/boot.elf" "$OUT_DIR/boot.bin"

################################################################################
# Compute kernel load base from boot.bin size (relative to BOOT_LOAD_ADDR)
################################################################################
BOOT_BIN="$OUT_DIR/boot.bin"

if BOOT_SIZE=$(stat -c%s "$BOOT_BIN" 2>/dev/null); then
  :
else
  BOOT_SIZE=$(stat -f%z "$BOOT_BIN")
fi

PAD_SIZE=$(( (ALIGN - (BOOT_SIZE % ALIGN)) % ALIGN ))
KERNEL_PHYS_BASE=$(( BOOT_LOAD_ADDR + BOOT_SIZE + PAD_SIZE ))
KERNEL_PHYS_BASE_HEX=$(printf "0x%X" "$KERNEL_PHYS_BASE")

KERNEL_VA_BASE=$(python3 - <<PY
hh=int("${HH_PHYS_4000_BASE}", 16)
ram=int("0x%X" % ${RAM_BASE}, 16)
kpa=int("${KERNEL_PHYS_BASE_HEX}", 16)
print(f"0x{(hh + (kpa - ram)) & ((1<<64)-1):X}")
PY
)

################################################################################
# Build kernel (stub)
################################################################################
"$CC" "${KERNEL_CFLAGS[@]}" -c "$KERNEL_DIR/Sources/Kernel/kernel_header.S" -o "$OUT_DIR/obj/kernel_header.o"
"$CC" "${KERNEL_CFLAGS[@]}" -c "$KERNEL_DIR/Sources/Kernel/kcrt0.c"         -o "$OUT_DIR/obj/kcrt0.o"
"$CC" "${KERNEL_CFLAGS[@]}" -c "$KERNEL_DIR/Sources/Kernel/kmain.c"         -o "$OUT_DIR/obj/kmain.o"
"$CC" "${KERNEL_CFLAGS[@]}" -c "$KERNEL_DIR/Sources/Kernel/mmu.c"           -o "$OUT_DIR/obj/mmu.o"
"$CC" "${KERNEL_CFLAGS[@]}" -c "$KERNEL_DIR/Sources/Kernel/dtb.c"           -o "$OUT_DIR/obj/dtb.o"
"$CC" "${KERNEL_CFLAGS[@]}" -c "$KERNEL_DIR/Sources/Kernel/mem.c"           -o "$OUT_DIR/obj/mem.o"
"$CC" "${KERNEL_CFLAGS[@]}" -c "$KERNEL_DIR/Sources/Kernel/kernel_vectors.S" -o "$OUT_DIR/obj/kernel_vectors.o"
"$CC" "${KERNEL_CFLAGS[@]}" -c "$KERNEL_DIR/Sources/HAL/uart_pl011.c"       -o "$OUT_DIR/obj/uart.o"

"$LD" -nostdlib -T "$KERNEL_DIR/Linker/kernel.ld" \
  --defsym=KERNEL_PHYS_BASE=$KERNEL_PHYS_BASE_HEX \
  --defsym=KERNEL_VA_BASE=$KERNEL_VA_BASE \
  "$OUT_DIR/obj/kernel_header.o" \
  "$OUT_DIR/obj/kcrt0.o" \
  "$OUT_DIR/obj/kmain.o" \
  "$OUT_DIR/obj/mmu.o" \
  "$OUT_DIR/obj/dtb.o" \
  "$OUT_DIR/obj/mem.o" \
  "$OUT_DIR/obj/kernel_vectors.o" \
  "$OUT_DIR/obj/uart.o" \
  -o "$OUT_DIR/kernel.elf"

"$OBJCOPY" -O binary "$OUT_DIR/kernel.elf" "$OUT_DIR/kernel.bin"

################################################################################
# Combine
################################################################################
KERNEL_BIN="$OUT_DIR/kernel.bin"
COMBINED_IMG="$OUT_DIR/kernel.img"

if [[ -f "$BOOT_BIN" && -f "$KERNEL_BIN" ]]; then
  BOOT_PAD="$OUT_DIR/boot.padded.bin"
  cp "$BOOT_BIN" "$BOOT_PAD"
  if [[ $PAD_SIZE -ne 0 ]]; then
    dd if=/dev/zero bs=1 count=$PAD_SIZE >> "$BOOT_PAD" 2>/dev/null
  fi
  cat "$BOOT_PAD" "$KERNEL_BIN" > "$COMBINED_IMG"
  rm -f "$BOOT_PAD"
fi

echo "Built boot and kernel images."
echo "BOOT_LOAD_ADDR = $(printf "0x%X" "$BOOT_LOAD_ADDR")"
echo "KERNEL_PHYS_BASE = $KERNEL_PHYS_BASE_HEX"
echo "KERNEL_VA_BASE   = $KERNEL_VA_BASE"
