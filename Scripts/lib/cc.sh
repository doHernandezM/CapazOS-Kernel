#!/usr/bin/env bash
set -euo pipefail

# Compile helpers for C/ASM

CC_LIB_THIS="${BASH_SOURCE[0]}"
CC_LIB_DIR="$(cd "$(dirname "$CC_LIB_THIS")" && pwd)"
# shellcheck source=common.sh
source "$CC_LIB_DIR/common.sh"

cc_compile() {
  local src="$1" obj="$2"; shift 2
  ensure_dir "$(dirname "$obj")"
  "$CC" "$@" -c "$src" -o "$obj"
}

cc_compile_many() {
  # Usage: cc_compile_many <out_dir> <cflags...> -- <src1> <src2> ...
  local out_dir="$1"; shift
  local -a cflags=()
  while [[ $# -gt 0 ]]; do
    if [[ "$1" == "--" ]]; then shift; break; fi
    cflags+=("$1"); shift
  done
  local src
  for src in "$@"; do
    local base; base="$(basename "$src")"
    local obj="$out_dir/${base%.*}.o"
    cc_compile "$src" "$obj" "${cflags[@]}"
  done
}
