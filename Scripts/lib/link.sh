#!/usr/bin/env bash
set -euo pipefail

LINK_LIB_THIS="${BASH_SOURCE[0]}"
LINK_LIB_DIR="$(cd "$(dirname "$LINK_LIB_THIS")" && pwd)"
# shellcheck source=common.sh
source "$LINK_LIB_DIR/common.sh"

link_elf() {
  local out="$1" linker_script="$2"; shift 2
  # Remaining args: obj files and ld args.
  ensure_dir "$(dirname "$out")"
  "$LD" -nostdlib -T "$linker_script" "$@" -o "$out"
}

objcopy_bin() {
  local elf="$1" bin="$2"
  ensure_dir "$(dirname "$bin")"
  "$OBJCOPY" -O binary "$elf" "$bin"
}

# Back-compat alias (older scripts used objcopy_binary).
objcopy_binary() {
  objcopy_bin "$@"
}

nm_sym() {
  local elf="$1" sym="$2"
  # Prints hex address without 0x prefix, or empty if missing.
  "$NM" -g --defined-only "$elf" | awk -v s="$sym" '$3==s {print $1}' | head -n1
}
