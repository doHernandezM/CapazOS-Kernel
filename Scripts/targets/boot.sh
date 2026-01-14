#!/usr/bin/env bash
set -euo pipefail

TARGET_THIS="${BASH_SOURCE[0]}"
TARGET_DIR="$(cd "$(dirname "$TARGET_THIS")" && pwd)"
# shellcheck source=../lib/cc.sh
source "${TARGET_DIR}/../lib/cc.sh"
# shellcheck source=../lib/link.sh
source "${TARGET_DIR}/../lib/link.sh"

build_boot() {
  local out_dir="$1" obj_dir="$2" kernel_dir="$3"

  local boot_ld="${kernel_dir}/Linker/boot.ld"
  local start_src="${kernel_dir}/Sources/Arch/aarch64/start.S"
  local start_obj="${obj_dir}/boot_start.o"

  cc_compile "$start_src" "$start_obj" "${BOOT_CFLAGS[@]}"
  link_elf "${out_dir}/boot.elf" "$boot_ld" "$start_obj"
  objcopy_binary "${out_dir}/boot.elf" "${out_dir}/boot.bin"
}
