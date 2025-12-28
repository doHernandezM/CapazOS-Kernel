#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
KERNEL_DIR="$ROOT/Kernel"
OUT_DIR="$ROOT/build"

source "$KERNEL_DIR/Scripts/toolchain.env"

mkdir -p "$OUT_DIR/obj"

#
# This script now produces two separate binaries: a minimal boot image
# (boot.elf) that runs in identity-mapped memory and brings up the MMU,
# and the higher-half kernel image (kernel.elf) that contains the main
# runtime. The boot image is linked with Kernel/Linker/boot.ld, while
# the kernel image is linked with Kernel/Linker/kernel.ld.
#

COMMON_CFLAGS=(
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

# Copy common flags for kernel and boot builds. Kernel_CFLAGS may be
# augmented by test defines. Boot_CFLAGS additionally defines BOOT_STAGE
# so that mmu_ttbr1.c and linker_symbols.h expose the appropriate
# interfaces.
KERNEL_CFLAGS=("${COMMON_CFLAGS[@]}")
BOOT_CFLAGS=("${COMMON_CFLAGS[@]}" -DBOOT_STAGE=1)

##
# Tests: build flags
KTEST_ENABLE=${KTEST_ENABLE:-0}
if [[ "$KTEST_ENABLE" == "1" ]]; then
  KERNEL_CFLAGS+=( -DKTEST_ENABLE=1 )
  # Optional debug flag for tests
  KERNEL_CFLAGS+=( -DKTEST_DEBUG=1 )
fi

##
##
# Build the kernel image first so that we can extract its section
# addresses and compute boot-time constants.  The kernel image excludes
# the boot assembly and bootstrap helpers but includes the full
# runtime and HAL.
##

# Compile kernel assembly/C sources. The kernel image excludes the boot
# assembly and bootstrap helpers but includes the full runtime and HAL.
"$CC" "${KERNEL_CFLAGS[@]}" -c "$KERNEL_DIR/Sources/Arch/aarch64/vectors.S" -o "$OUT_DIR/obj/vectors.o"

"$CC" "${KERNEL_CFLAGS[@]}" -c "$KERNEL_DIR/Sources/MMU/mmu.c"             -o "$OUT_DIR/obj/mmu.o"
"$CC" "${KERNEL_CFLAGS[@]}" -c "$KERNEL_DIR/Sources/MMU/mmu_ttbr1.c"       -o "$OUT_DIR/obj/mmu_ttbr1.o"
"$CC" "${KERNEL_CFLAGS[@]}" -c "$KERNEL_DIR/Sources/MMU/mmu_task_space.c"  -o "$OUT_DIR/obj/mmu_task_space.o"
"$CC" "${KERNEL_CFLAGS[@]}" -c "$KERNEL_DIR/Sources/MMU/vm_layout.c"       -o "$OUT_DIR/obj/vm_layout.o"
"$CC" "${KERNEL_CFLAGS[@]}" -c "$KERNEL_DIR/Support/include/capability.c"    -o "$OUT_DIR/obj/capability.o"
"$CC" "${KERNEL_CFLAGS[@]}" -c "$KERNEL_DIR/Support/include/work_request.c"  -o "$OUT_DIR/obj/work_request.o"
"$CC" "${KERNEL_CFLAGS[@]}" -c "$KERNEL_DIR/Sources/KernelC/mem.c"          -o "$OUT_DIR/obj/mem.o"
"$CC" "${KERNEL_CFLAGS[@]}" -c "$KERNEL_DIR/Sources/KernelC/cpu.c"          -o "$OUT_DIR/obj/cpu.o"
"$CC" "${KERNEL_CFLAGS[@]}" -c "$KERNEL_DIR/Sources/KernelC/kernel_test.c"   -o "$OUT_DIR/obj/kernel_test.o"
"$CC" "${KERNEL_CFLAGS[@]}" -c "$KERNEL_DIR/Sources/Runtime/crt0.c"         -o "$OUT_DIR/obj/crt0.o"
"$CC" "${KERNEL_CFLAGS[@]}" -c "$KERNEL_DIR/Sources/HAL/uart_pl011.c"       -o "$OUT_DIR/obj/uart.o"
"$CC" "${KERNEL_CFLAGS[@]}" -c "$KERNEL_DIR/Sources/KernelC/exceptions.c"   -o "$OUT_DIR/obj/exceptions.o"
"$CC" "${KERNEL_CFLAGS[@]}" -c "$KERNEL_DIR/Sources/KernelC/kmain.c"        -o "$OUT_DIR/obj/kmain.o"
"$CC" "${KERNEL_CFLAGS[@]}" -c "$KERNEL_DIR/Sources/Runtime/embedded_shims.c" -o "$OUT_DIR/obj/embedded_shims.o"
"$CC" "${KERNEL_CFLAGS[@]}" -c "$KERNEL_DIR/Support/include/kiface.c"       -o "$OUT_DIR/obj/kiface.o"
"$CC" "${KERNEL_CFLAGS[@]}" -c "$KERNEL_DIR/Support/include/vm_object.c"    -o "$OUT_DIR/obj/vm_object.o"

# Swift (Embedded) -> object for the kernel image
"$SWIFTC" \
  -target aarch64-none-none-elf \
  -emit-object -parse-as-library -wmo \
  -Xfrontend -enable-experimental-feature \
  -Xfrontend Embedded \
  -Xfrontend -disable-stack-protector \
  "$KERNEL_DIR/Sources/KernelSwift/KernelMain.swift" \
  -o "$OUT_DIR/obj/KernelMain.o"

# Link the kernel image
"$LD" -nostdlib -T "$KERNEL_DIR/Support/kernel.ld" \
  "$OUT_DIR/obj/vectors.o" \
  "$OUT_DIR/obj/capability.o" \
  "$OUT_DIR/obj/work_request.o" \
  "$OUT_DIR/obj/mmu.o" \
  "$OUT_DIR/obj/mmu_ttbr1.o" \
  "$OUT_DIR/obj/mmu_task_space.o" \
  "$OUT_DIR/obj/vm_layout.o" \
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
  -o "$OUT_DIR/kernel.elf"

# Produce a flat binary for the kernel image (optional)
"$OBJCOPY" -O binary "$OUT_DIR/kernel.elf" "$OUT_DIR/kernel.bin"

##
# Extract kernel section addresses and compute boot configuration values.
# Use llvm-nm to read the addresses of symbols exported by the kernel
# linker script.  Construct additional C preprocessor defines for the
# boot build.

# Helper function: extract a symbol value from kernel.elf using nm
symbol_value() {
    local sym="$1"
    "$NM" -g --defined-only "$OUT_DIR/kernel.elf" | awk -v sym="$1" '$3 == sym {print $1}' | head -n1
}

# Read the physical addresses of various kernel sections
text_start_hex=$(symbol_value __text_start_phys)
text_end_hex=$(symbol_value __text_end_phys)
rodata_start_hex=$(symbol_value __rodata_start_phys)
rodata_end_hex=$(symbol_value __rodata_end_phys)
data_start_hex=$(symbol_value __data_start_phys)
bss_end_hex=$(symbol_value __bss_end_phys)
pt_base_hex=$(symbol_value __pt_base_phys)
pt_end_hex=$(symbol_value __pt_end_phys)
stack_base_hex=$(symbol_value __stack_bottom_phys)
stack_end_hex=$(symbol_value __stack_top_phys)
crt0_phys_hex=$(symbol_value crt0_phys)

# The physical base of the kernel image is the start of the text segment.
# Remove the 0x prefix when constructing the base hex string.
kernel_phys_base_hex=${text_start_hex#0x}

# Compute the entry offset as (crt0_phys - kernel_phys_base).  Convert
# the addresses to decimal for arithmetic, then back to hex.
crt0_phys_dec=$((0x${crt0_phys_hex}))
phys_base_dec=$((0x${kernel_phys_base_hex}))
entry_offset_dec=$((crt0_phys_dec - phys_base_dec))
entry_offset_hex=$(printf "%llx" $entry_offset_dec)

# Compose extra defines to pass to the boot compilation.  Use plain
# hexadecimal constants (no ULL suffix) so that they are valid in both
# C and assembly contexts.  Prefix values with 0x.
BOOT_EXTRA_DEFINES=(
  -DKERNEL_PHYS_BASE=0x${kernel_phys_base_hex}
  -DKERNEL_PHYS_END=0x${stack_end_hex}
  -DKERNEL_ENTRY_OFFSET=0x${entry_offset_hex}
  -DKERNEL_PHYS_TEXT_START=0x${text_start_hex}
  -DKERNEL_PHYS_TEXT_END=0x${text_end_hex}
  -DKERNEL_PHYS_RODATA_START=0x${rodata_start_hex}
  -DKERNEL_PHYS_RODATA_END=0x${rodata_end_hex}
  -DKERNEL_PHYS_DATA_START=0x${data_start_hex}
  -DKERNEL_PHYS_BSS_END=0x${bss_end_hex}
  -DKERNEL_PHYS_PT_BASE=0x${pt_base_hex}
  -DKERNEL_PHYS_PT_END=0x${pt_end_hex}
  -DKERNEL_PHYS_STACK_BASE=0x${stack_base_hex}
  -DKERNEL_PHYS_STACK_END=0x${stack_end_hex}
)

##
# Build the boot image using the extracted constants
##

# Compile boot assembly/C sources. Only a minimal set of sources are needed
# to get the MMU enabled and transfer control to the kernel.  mmu_ttbr1.c
# is compiled with BOOT_STAGE defined so that only mmu_bootstrap() and
# related helpers are emitted.  We also pass the computed defines so
# that boot_config constants reflect the actual kernel layout.
"$CC" "${BOOT_CFLAGS[@]}" "${BOOT_EXTRA_DEFINES[@]}" -c "$KERNEL_DIR/Sources/Arch/aarch64/start.S" -o "$OUT_DIR/obj/boot_start.o"
"$CC" "${BOOT_CFLAGS[@]}" "${BOOT_EXTRA_DEFINES[@]}" -c "$KERNEL_DIR/Sources/Arch/aarch64/boot_higherhalf.S" -o "$OUT_DIR/obj/boot_higherhalf.o"
"$CC" "${BOOT_CFLAGS[@]}" "${BOOT_EXTRA_DEFINES[@]}" -DBOOT_STAGE=1 -c "$KERNEL_DIR/Sources/MMU/mmu_ttbr1.c" -o "$OUT_DIR/obj/mmu_ttbr1_boot.o"

# Link the boot image
"$LD" -nostdlib -T "$KERNEL_DIR/Support/boot.ld" \
  "$OUT_DIR/obj/boot_start.o" \
  "$OUT_DIR/obj/boot_higherhalf.o" \
  "$OUT_DIR/obj/mmu_ttbr1_boot.o" \
  -o "$OUT_DIR/boot.elf"

# Produce a flat binary for the boot image (optional)
"$OBJCOPY" -O binary "$OUT_DIR/boot.elf" "$OUT_DIR/boot.bin"

echo "Built boot and kernel images:"
ls -lh "$OUT_DIR/boot.elf" "$OUT_DIR/kernel.elf"

#
# Combine the boot and kernel binaries into a single flat image.  The
# boot image is linked at the fixed physical base (0x4000_0000) and
# brings up the MMU before transferring control to the higher‑half
# kernel.  The kernel image is linked such that its first loadable
# segment begins immediately after the boot region when loaded at
# its physical address.  To ensure proper separation we pad the boot
# binary to a 2 MiB boundary.  The resulting `kernel.img` can be used
# by QEMU’s `-kernel` option or loaded via a loader device.

BOOT_BIN="$OUT_DIR/boot.bin"
KERNEL_BIN="$OUT_DIR/kernel.bin"
COMBINED_IMG="$OUT_DIR/kernel.img"

# Only create a combined image if both binaries exist
if [[ -f "$BOOT_BIN" && -f "$KERNEL_BIN" ]]; then
  # Determine the boot binary size in a cross-platform way.  GNU stat
  # supports -c %s while BSD/macOS stat uses -f %z.  Try GNU first
  # and fall back to BSD.
  if BOOT_SIZE=$(stat -c%s "$BOOT_BIN" 2>/dev/null); then
    :
  else
    BOOT_SIZE=$(stat -f%z "$BOOT_BIN")
  fi
  ALIGN=2097152 # 2 MiB
  PAD_SIZE=$(( (ALIGN - (BOOT_SIZE % ALIGN)) % ALIGN ))
  # Create a temporary padded copy of the boot binary
  BOOT_PAD="$OUT_DIR/boot.padded.bin"
  cp "$BOOT_BIN" "$BOOT_PAD"
  if [[ $PAD_SIZE -ne 0 ]]; then
    dd if=/dev/zero bs=1 count=$PAD_SIZE >> "$BOOT_PAD" 2>/dev/null
  fi
  # Concatenate the padded boot image and kernel image
  cat "$BOOT_PAD" "$KERNEL_BIN" > "$COMBINED_IMG"
  rm -f "$BOOT_PAD"
  echo "Combined image created at $COMBINED_IMG"
fi
