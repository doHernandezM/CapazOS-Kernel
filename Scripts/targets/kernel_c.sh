#!/usr/bin/env bash
set -euo pipefail

TARGET_THIS="${BASH_SOURCE[0]}"
TARGET_DIR="$(cd "$(dirname "$TARGET_THIS")" && pwd)"

# shellcheck source=boot.sh
source "${TARGET_DIR}/boot.sh"
# shellcheck source=core_c.sh
source "${TARGET_DIR}/core_c.sh"
# shellcheck source=../lib/cc.sh
source "${TARGET_DIR}/../lib/cc.sh"
# shellcheck source=../lib/link.sh
source "${TARGET_DIR}/../lib/link.sh"
# shellcheck source=../lib/pack.sh
source "${TARGET_DIR}/../lib/pack.sh"

build_kernel_c() {
  # Args are passed by Scripts/build.sh as:
  #   build_kernel_c <out_dir> <obj_dir> <kernel_dir>
  # (Some earlier revisions passed an extra leading <root_dir>.)
  local out_dir obj_dir kernel_dir
  if [[ $# -ge 4 ]]; then
    out_dir="$2"
    obj_dir="$3"
    kernel_dir="$4"
  else
    out_dir="$1"
    obj_dir="$2"
    kernel_dir="$3"
  fi

  # Ensure boot exists (boot.bin used for sizing).
  build_boot "$out_dir" "$obj_dir" "$kernel_dir"

  local boot_bin="$out_dir/boot.bin"
  local boot_size pad_size
  boot_size=$(file_size "$boot_bin")
  pad_size=$(( (ALIGN - (boot_size % ALIGN)) % ALIGN ))

  local kernel_phys_base=$(( BOOT_LOAD_ADDR + boot_size + pad_size ))
  local kernel_phys_base_hex
  kernel_phys_base_hex=$(printf "0x%X" "$kernel_phys_base")

  local kernel_va_base
  kernel_va_base=$(python3 - <<PY
hh=int("${HH_PHYS_4000_BASE}", 16)
ram=int("0x%X" % ${RAM_BASE}, 16)
kpa=int("${kernel_phys_base_hex}", 16)
print(f"0x{(hh + (kpa - ram)) & ((1<<64)-1):X}")
PY
)

  # Compile kernel objects
  local objs=()
  local add_obj
  add_obj() {
    local src="$1"; shift
    local base
    base=$(basename "$src")
    local obj="$obj_dir/${base%.*}.o"
    cc_compile "$src" "$obj" "${KERNEL_CFLAGS[@]}" "$@"
    objs+=("$obj")
  }

  add_obj "$kernel_dir/Sources/Kernel/boot/kernel_header.S"
  add_obj "$kernel_dir/Sources/Kernel/boot/kcrt0.c"
  add_obj "$kernel_dir/Sources/Kernel/kmain.c"
  add_obj "$kernel_dir/Sources/Kernel/core_abi_v1.c"
  add_obj "$kernel_dir/Sources/Kernel/core_abi_v2.c"
  add_obj "$kernel_dir/Sources/Kernel/platform/platform.c"
  add_obj "$kernel_dir/Sources/Kernel/mm/pmm.c"
  add_obj "$kernel_dir/Sources/Kernel/alloc/slab_cache.c"
  add_obj "$kernel_dir/Sources/Kernel/alloc/alloc_stats.c"
  add_obj "$kernel_dir/Sources/Kernel/work/work_queue.c"
  add_obj "$kernel_dir/Sources/Kernel/cap/cap_entry.c"
  add_obj "$kernel_dir/Sources/Kernel/cap/cap_table.c"
  add_obj "$kernel_dir/Sources/Kernel/cap/cap_ops.c"
  add_obj "$kernel_dir/Sources/Kernel/cap/cap_status_ks.c"
  add_obj "$kernel_dir/Sources/Kernel/task/task.c"
  add_obj "$kernel_dir/Sources/Kernel/ipc/endpoint.c"
  add_obj "$kernel_dir/Sources/Kernel/ipc/ipc_message.c"
  add_obj "$kernel_dir/Sources/Kernel/kheap.c"
  add_obj "$kernel_dir/Sources/Kernel/mm/mmu.c"
  add_obj "$kernel_dir/Sources/Kernel/platform/dtb.c"
  add_obj "$kernel_dir/Sources/Kernel/util/MathHelper.c"
  add_obj "$kernel_dir/Sources/Kernel/mm/mem.c"
  add_obj "$kernel_dir/Sources/Kernel/debug/panic.c"
  add_obj "$kernel_dir/Sources/Arch/aarch64/exception/kernel_vectors.S"
  add_obj "$kernel_dir/Sources/Kernel/irg/irq.c"
  add_obj "$kernel_dir/Sources/Kernel/sched/deadline_queue.c"
  add_obj "$kernel_dir/Sources/Arch/aarch64/context_switch.S"
  add_obj "$kernel_dir/Sources/Kernel/sched/thread.c"
  add_obj "$kernel_dir/Sources/Kernel/sched/sched.c"
  add_obj "$kernel_dir/Sources/Kernel/sched/preempt.c"
  add_obj "$kernel_dir/Sources/HAL/gicv2.c"
  add_obj "$kernel_dir/Sources/HAL/timer_generic.c"
  add_obj "$kernel_dir/Sources/HAL/uart_pl011.c"

  # Optional Core(C): build and link if present.
  # Controlled by CORE_MODE=auto|on|off (set by Scripts/build.sh).
  case "${CORE_MODE:-auto}" in
    off)
      ;;
    on|auto)
      if [[ -n "${CORE_DIR:-}" ]] && build_core_c "$out_dir" "$obj_dir" "$kernel_dir" "$CORE_DIR"; then
        objs+=("$obj_dir/core_c.o")
      else
        if [[ "${CORE_MODE:-auto}" == "on" ]]; then
          die "CORE_MODE=on but no Core sources found under: ${CORE_DIR:-<unset>}/Sources"
        fi
      fi
      ;;
    *)
      die "Invalid CORE_MODE=${CORE_MODE} (expected auto|on|off)"
      ;;
  esac

  local kernel_ld="$kernel_dir/Linker/kernel.ld"

  # Link + objcopy
  link_elf "$out_dir/kernel.elf" "$kernel_ld" \
    --defsym=KERNEL_PHYS_BASE="$kernel_phys_base_hex" \
    --defsym=KERNEL_VA_BASE="$kernel_va_base" \
    "${objs[@]}"

  objcopy_binary "$out_dir/kernel.elf" "$out_dir/kernel.bin"

  # Combine
  combine_boot_and_kernel "$out_dir/boot.bin" "$out_dir/kernel.bin" "$out_dir/kernel.img" "$ALIGN"

  # Export computed values for logging
  echo "BOOT_LOAD_ADDR    = $(printf '0x%X' "$BOOT_LOAD_ADDR")"
  echo "KERNEL_PHYS_BASE  = $kernel_phys_base_hex"
  echo "KERNEL_VA_BASE    = $kernel_va_base"
}
