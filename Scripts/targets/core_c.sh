#!/usr/bin/env bash
set -euo pipefail

TARGET_THIS="${BASH_SOURCE[0]}"
TARGET_DIR="$(cd "$(dirname "$TARGET_THIS")" && pwd)"

# shellcheck source=../lib/cc.sh
source "${TARGET_DIR}/../lib/cc.sh"

_core_c_print_includes() {
  local -a args=("$@")
  local a
  echo "Core(C) include search paths:" >&2
  for a in "${args[@]}"; do
    if [[ "$a" == -I* ]]; then
      echo "  ${a}" >&2
    fi
  done
}

_core_c_forbidden_include_check() {
  local core_src_dir="$1"
  # Deny a small set of known kernel-private headers with clear error messages.
  # This is a diagnostic aid; the real enforcement is restricted include paths.
  local -a patterns=(
    '^[[:space:]]*#[[:space:]]*include[[:space:]]*"mmu\.h"'
    '^[[:space:]]*#[[:space:]]*include[[:space:]]*"pmm\.h"'
    '^[[:space:]]*#[[:space:]]*include[[:space:]]*"uart_pl011\.h"'
    '^[[:space:]]*#[[:space:]]*include[[:space:]]*"sched\.h"'
    '^[[:space:]]*#[[:space:]]*include[[:space:]]*"irq\.h"'
    '^[[:space:]]*#[[:space:]]*include[[:space:]]*"kheap\.h"'
    '^[[:space:]]*#[[:space:]]*include[[:space:]]*"panic\.h"'
    '^[[:space:]]*#[[:space:]]*include[[:space:]]*"timer_generic\.h"'
    '^[[:space:]]*#[[:space:]]*include[[:space:]]*"gicv2\.h"'
  )

  local p
  for p in "${patterns[@]}"; do
    if grep -RInE "$p" "$core_src_dir" >/dev/null 2>&1; then
      echo "Core(C) attempted to include a kernel-private header." >&2
      echo "Core may only include core_kernel_abi.h + Core headers." >&2
      echo "Matches:" >&2
      grep -RInE "$p" "$core_src_dir" >&2 || true
      return 1
    fi
  done
  return 0
}

build_core_c() {
  # Args:
  #   build_core_c <out_dir> <obj_dir> <kernel_dir> <core_dir>
  local out_dir="$1" obj_dir="$2" kernel_dir="$3" core_dir="$4"

  local core_src_dir="$core_dir/Sources"
  if [[ ! -d "$core_src_dir" ]]; then
    return 1
  fi

  # Discover sources (C + optional asm).
  local -a srcs=()
  while IFS= read -r -d '' f; do srcs+=("$f"); done < <(find "$core_src_dir" -type f \( -name '*.c' -o -name '*.S' \) -print0)

  if [[ ${#srcs[@]} -eq 0 ]]; then
    return 1
  fi

  # Optional diagnostic enforcement: fail fast on obviously forbidden includes.
  if ! _core_c_forbidden_include_check "$core_src_dir"; then
    return 2
  fi

  # Build flags:
  # Start from KERNEL_CFLAGS but strip *all* include-path flags so Core cannot see kernel internals.
  local -a base=()
  local i=0
  while [[ $i -lt ${#KERNEL_CFLAGS[@]} ]]; do
    local a="${KERNEL_CFLAGS[$i]}"
    case "$a" in
      -I)
        i=$((i+2)); continue ;;
      -isystem)
        i=$((i+2)); continue ;;
      -iquote)
        i=$((i+2)); continue ;;
      -I*|-isystem*|-iquote*)
        i=$((i+1)); continue ;;
      *)
        base+=("$a")
        i=$((i+1))
        ;;
    esac
  done

  # Restricted includes for Core.
  base+=(
    -I"$core_src_dir"
    -I"$kernel_dir/Sources/ABI"
  )
  if [[ -d "$out_dir/include" ]]; then
    base+=(-I"$out_dir/include")
  fi

  local -a objs=()
  local src rel obj
  for src in "${srcs[@]}"; do
    rel="${src#$core_dir/}"
    if [[ "$src" == *.c ]]; then
      obj="$obj_dir/core/${rel%.c}.o"
    else
      obj="$obj_dir/core/${rel%.S}.o"
    fi
    ensure_dir "$(dirname "$obj")"

    if ! cc_compile "$src" "$obj" "${base[@]}"; then
      _core_c_print_includes "${base[@]}"
      echo "Core(C) compilation failed. Core may only include ABI + Core headers." >&2
      return 2
    fi
    objs+=("$obj")
  done

  # Link into a single relocatable.
  local out_obj="$obj_dir/core_c.o"
  "$LD" -r -o "$out_obj" "${objs[@]}"

  log "Core(C) linked -> $out_obj"
  return 0
}
