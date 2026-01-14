#!/usr/bin/env bash
set -euo pipefail

SWIFT_LIB_THIS="${BASH_SOURCE[0]}"
SWIFT_LIB_DIR="$(cd "$(dirname "$SWIFT_LIB_THIS")" && pwd)"
# shellcheck source=common.sh
source "$SWIFT_LIB_DIR/common.sh"

# Compile Swift (Embedded) into a single object file.
#
# Usage:
#   swift_embed_compile <source.swift> <out.o> [extra swift flags...]
#
# toolchain.env must define SWIFTC.
swift_embed_compile() {
  local src="$1" obj="$2"; shift 2
  ensure_dir "$(dirname "$obj")"

  "$SWIFTC" \
    -target aarch64-none-none-elf \
    -emit-object \
    -parse-as-library \
    -wmo \
    -Xfrontend -enable-experimental-feature \
    -Xfrontend Embedded \
    -Xfrontend -disable-stack-protector \
    "$@" \
    "$src" \
    -o "$obj"
}
