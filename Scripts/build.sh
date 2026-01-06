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
  -I"$KERNEL_DIR/Sources/Kernel"
)

BOOT_CFLAGS=("${COMMON_CFLAGS[@]}" -DBOOT_STAGE=1)
KERNEL_CFLAGS=("${COMMON_CFLAGS[@]}")

# Timer/tick configuration (optional overrides).
# Usage examples:
#   CONFIG_TICKLESS=1 ./Kernel/Scripts/build.sh
#   CONFIG_TICK_HZ=250 ./Kernel/Scripts/build.sh
if [[ -n "${CONFIG_TICK_HZ:-}" ]]; then
  KERNEL_CFLAGS+=("-DCONFIG_TICK_HZ=${CONFIG_TICK_HZ}")
fi
if [[ -n "${CONFIG_TICKLESS:-}" ]]; then
  KERNEL_CFLAGS+=("-DCONFIG_TICKLESS=${CONFIG_TICKLESS}")
fi

# Optional: compile-time deliberate fault for exception-dump smoke testing.
# Usage: CAPAZ_FAULT_TEST=1 ./Kernel/Scripts/build.sh
if [[ "${CAPAZ_FAULT_TEST:-0}" != "0" ]]; then
  KERNEL_CFLAGS+=("-DCAPAZ_FAULT_TEST=${CAPAZ_FAULT_TEST}")
fi


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
"$CC" "${KERNEL_CFLAGS[@]}" -c "$KERNEL_DIR/Sources/Kernel/platform.c"      -o "$OUT_DIR/obj/platform.o"
"$CC" "${KERNEL_CFLAGS[@]}" -c "$KERNEL_DIR/Sources/Kernel/pmm.c"           -o "$OUT_DIR/obj/pmm.o"
"$CC" "${KERNEL_CFLAGS[@]}" -c "$KERNEL_DIR/Sources/Kernel/kheap.c"         -o "$OUT_DIR/obj/kheap.o"
"$CC" "${KERNEL_CFLAGS[@]}" -c "$KERNEL_DIR/Sources/Kernel/mmu.c"           -o "$OUT_DIR/obj/mmu.o"
"$CC" "${KERNEL_CFLAGS[@]}" -c "$KERNEL_DIR/Sources/Kernel/dtb.c"           -o "$OUT_DIR/obj/dtb.o"
"$CC" "${KERNEL_CFLAGS[@]}" -c "$KERNEL_DIR/Sources/Kernel/MathHelper.c"   -o "$OUT_DIR/obj/math_helper.o"
"$CC" "${KERNEL_CFLAGS[@]}" -c "$KERNEL_DIR/Sources/Kernel/mem.c"           -o "$OUT_DIR/obj/mem.o"
"$CC" "${KERNEL_CFLAGS[@]}" -c "$KERNEL_DIR/Sources/Kernel/panic.c"         -o "$OUT_DIR/obj/panic.o"
"$CC" "${KERNEL_CFLAGS[@]}" -c "$KERNEL_DIR/Sources/Kernel/kernel_vectors.S" -o "$OUT_DIR/obj/kernel_vectors.o"
"$CC" "${KERNEL_CFLAGS[@]}" -c "$KERNEL_DIR/Sources/Kernel/irq.c"           -o "$OUT_DIR/obj/irq.o"
"$CC" "${KERNEL_CFLAGS[@]}" -c "$KERNEL_DIR/Sources/Kernel/deadline_queue.c" -o "$OUT_DIR/obj/deadline_queue.o"
"$CC" "${KERNEL_CFLAGS[@]}" -c "$KERNEL_DIR/Sources/Arch/aarch64/context_switch.S" -o "$OUT_DIR/obj/context_switch.o"
"$CC" "${KERNEL_CFLAGS[@]}" -c "$KERNEL_DIR/Sources/Kernel/thread.c"        -o "$OUT_DIR/obj/thread.o"
"$CC" "${KERNEL_CFLAGS[@]}" -c "$KERNEL_DIR/Sources/Kernel/sched.c"         -o "$OUT_DIR/obj/sched.o"

"$CC" "${KERNEL_CFLAGS[@]}" -c "$KERNEL_DIR/Sources/HAL/gicv2.c"           -o "$OUT_DIR/obj/gicv2.o"
"$CC" "${KERNEL_CFLAGS[@]}" -c "$KERNEL_DIR/Sources/HAL/timer_generic.c"   -o "$OUT_DIR/obj/timer_generic.o"
"$CC" "${KERNEL_CFLAGS[@]}" -c "$KERNEL_DIR/Sources/HAL/uart_pl011.c"       -o "$OUT_DIR/obj/uart.o"

"$LD" -nostdlib -T "$KERNEL_DIR/Linker/kernel.ld" \
  --defsym=KERNEL_PHYS_BASE=$KERNEL_PHYS_BASE_HEX \
  --defsym=KERNEL_VA_BASE=$KERNEL_VA_BASE \
  "$OUT_DIR/obj/kernel_header.o" \
  "$OUT_DIR/obj/kcrt0.o" \
  "$OUT_DIR/obj/kmain.o" \
   "$OUT_DIR/obj/platform.o" \
   "$OUT_DIR/obj/mmu.o" \
  "$OUT_DIR/obj/dtb.o" \
  "$OUT_DIR/obj/pmm.o" \
  "$OUT_DIR/obj/kheap.o" \
  "$OUT_DIR/obj/math_helper.o" \
  "$OUT_DIR/obj/mem.o" \
  "$OUT_DIR/obj/panic.o" \
  "$OUT_DIR/obj/irq.o" \
  "$OUT_DIR/obj/deadline_queue.o" \
  "$OUT_DIR/obj/context_switch.o" \
  "$OUT_DIR/obj/thread.o" \
  "$OUT_DIR/obj/sched.o" \
  "$OUT_DIR/obj/gicv2.o" \
  "$OUT_DIR/obj/timer_generic.o" \
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

################################################################################
# Package Kernel directory into ROOT/Kernel.zip (macOS-friendly)
################################################################################
KERNEL_ZIP="$ROOT/Kernel.zip"

if ! command -v zip >/dev/null 2>&1; then
  echo "Error: 'zip' not found. Install it or ensure it's available in PATH." >&2
  exit 1
fi

rm -f "$KERNEL_ZIP"
(
  cd "$KERNEL_DIR"
  # Zip the *contents* of Kernel/ (not the absolute path).
  # -r: recursive, -q: quiet
  zip -rq "$KERNEL_ZIP" .
)

echo "Built boot and kernel images."
echo "BOOT_LOAD_ADDR = $(printf "0x%X" "$BOOT_LOAD_ADDR")"
echo "KERNEL_PHYS_BASE = $KERNEL_PHYS_BASE_HEX"
echo "KERNEL_VA_BASE   = $KERNEL_VA_BASE"
echo "Packaged Kernel directory: $KERNEL_ZIP"
