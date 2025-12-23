#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
KERNEL_DIR="$ROOT/Kernel"
OUT_DIR="$ROOT/build"

source "$KERNEL_DIR/Scripts/toolchain.env"

mkdir -p "$OUT_DIR/obj"

CFLAGS=(
  -ffreestanding -fno-builtin -fno-stack-protector
  -fno-pic -fno-pie
  -Wall -Wextra
  -mcpu=cortex-a72
  -target aarch64-none-none-elf
  -g -O0
  
  -I"$KERNEL_DIR/Support"
  -I"$KERNEL_DIR/Support/include"
  -I"$KERNEL_DIR/Sources/HAL"
  -I"$KERNEL_DIR/Sources"
  -I"$KERNEL_DIR/Sources/MMU"
  -I"$KERNEL_DIR/Sources/Arch/aarch64"
  -I"$KERNEL_DIR/Sources/Support"
  -I"$KERNEL_DIR/Sources/KernelC"
  -I"$KERNEL_DIR/Sources/Runtime"
  -I"$KERNEL_DIR/Sources/KernelInterfaces"   # NEW: Phase 0 headers
)

# Enable kernel tests (set to 0/1)
KTEST_ENABLE=${KTEST_ENABLE:-0}
if [[ "$KTEST_ENABLE" == "1" ]]; then
  CFLAGS+=( -DKTEST_ENABLE=1 )
  # Optional:
  CFLAGS+=( -DKTEST_DEBUG=1 )
fi

LDFLAGS=(
  -nostdlib
  -T "$KERNEL_DIR/Support/linker.ld"
)

# Assemble / compile
"$CC" "${CFLAGS[@]}" -c "$KERNEL_DIR/Sources/Arch/aarch64/start.S" -o "$OUT_DIR/obj/start.o"
"$CC" "${CFLAGS[@]}" -c "$KERNEL_DIR/Sources/Arch/aarch64/vectors.S" -o "$OUT_DIR/obj/vectors.o"
"$CC" "${CFLAGS[@]}" -c "$KERNEL_DIR/Sources/MMU/mmu.c"             -o "$OUT_DIR/obj/mmu.o"
"$CC" "${CFLAGS[@]}" -c "$KERNEL_DIR/Support/include/capability.c"             -o "$OUT_DIR/obj/capability.o"
"$CC" "${CFLAGS[@]}" -c "$KERNEL_DIR/Support/include/work_request.c"             -o "$OUT_DIR/obj/work_request.o"
"$CC" "${CFLAGS[@]}" -c "$KERNEL_DIR/Sources/KernelC/mem.c"         -o "$OUT_DIR/obj/mem.o"
"$CC" "${CFLAGS[@]}" -c "$KERNEL_DIR/Sources/KernelC/cpu.c"         -o "$OUT_DIR/obj/cpu.o"
"$CC" "${CFLAGS[@]}" -c "$KERNEL_DIR/Sources/KernelC/kernel_test.c"         -o "$OUT_DIR/obj/kernel_test.o"
"$CC" "${CFLAGS[@]}" -c "$KERNEL_DIR/Sources/Runtime/crt0.c"        -o "$OUT_DIR/obj/crt0.o"
"$CC" "${CFLAGS[@]}" -c "$KERNEL_DIR/Sources/HAL/uart_pl011.c"      -o "$OUT_DIR/obj/uart.o"
"$CC" "${CFLAGS[@]}" -c "$KERNEL_DIR/Sources/KernelC/exceptions.c"  -o "$OUT_DIR/obj/exceptions.o"
"$CC" "${CFLAGS[@]}" -c "$KERNEL_DIR/Sources/KernelC/kmain.c"       -o "$OUT_DIR/obj/kmain.o"
"$CC" "${CFLAGS[@]}" -c "$KERNEL_DIR/Sources/Runtime/embedded_shims.c" -o "$OUT_DIR/obj/embedded_shims.o"
"$CC" "${CFLAGS[@]}" -c "$KERNEL_DIR/Support/include/kiface.c"    -o "$OUT_DIR/obj/kiface.o"
"$CC" "${CFLAGS[@]}" -c "$KERNEL_DIR/Support/include/vm_object.c" -o "$OUT_DIR/obj/vm_object.o"

# Swift (Embedded) -> object
"$SWIFTC" \
  -target aarch64-none-none-elf \
  -emit-object -parse-as-library -wmo \
  -Xfrontend -enable-experimental-feature \
  -Xfrontend Embedded \
  -Xfrontend -disable-stack-protector \
  "$KERNEL_DIR/Sources/KernelSwift/KernelMain.swift" \
  -o "$OUT_DIR/obj/KernelMain.o"

# Link ELF
"$LD" "${LDFLAGS[@]}" \
  "$OUT_DIR/obj/start.o" \
  "$OUT_DIR/obj/vectors.o" \
  "$OUT_DIR/obj/capability.o" \
  "$OUT_DIR/obj/work_request.o" \
  "$OUT_DIR/obj/mmu.o" \
  "$OUT_DIR/obj/mem.o" \
  "$OUT_DIR/obj/cpu.o" \
  "$OUT_DIR/obj/kernel_test.o" \
  "$OUT_DIR/obj/crt0.o" \
  "$OUT_DIR/obj/uart.o" \
  "$OUT_DIR/obj/kmain.o" \
  "$OUT_DIR/obj/exceptions.o" \
  "$OUT_DIR/obj/kiface.o" \
  "$OUT_DIR/obj/vm_object.o" \
  "$OUT_DIR/obj/KernelMain.o" \
  "$OUT_DIR/obj/embedded_shims.o" \
  -o "$OUT_DIR/Kernel.elf"

# Flat binary (optional)
"$OBJCOPY" -O binary "$OUT_DIR/Kernel.elf" "$OUT_DIR/Kernel.bin"

echo "Built:"
ls -lh "$OUT_DIR/Kernel.elf" "$OUT_DIR/Kernel.bin"
