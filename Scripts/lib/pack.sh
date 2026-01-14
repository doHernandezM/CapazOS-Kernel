#!/usr/bin/env bash
set -euo pipefail

PACK_LIB_THIS="${BASH_SOURCE[0]}"
PACK_LIB_DIR="$(cd "$(dirname "$PACK_LIB_THIS")" && pwd)"
# shellcheck source=common.sh
source "$PACK_LIB_DIR/common.sh"

pad_to_alignment() {
  local in_file="$1" out_file="$2" align_bytes="$3"
  cp "$in_file" "$out_file"
  local size pad
  size=$(file_size "$in_file")
  pad=$(( (align_bytes - (size % align_bytes)) % align_bytes ))
  if [[ "$pad" -ne 0 ]]; then
    dd if=/dev/zero bs=1 count="$pad" >> "$out_file" 2>/dev/null
  fi
  echo "$pad"
}

concat_image() {
  local boot_bin="$1" kernel_bin="$2" out_img="$3" align_bytes="$4"
  local tmp
  tmp="$(mktemp -t boot.padded.XXXXXXXX.bin)"
  pad_to_alignment "$boot_bin" "$tmp" "$align_bytes" >/dev/null
  cat "$tmp" "$kernel_bin" > "$out_img"
  rm -f "$tmp"
}

# Backwards-compatible name used by some targets.
# Signature matches: (boot.bin, kernel.bin, out.img, align_bytes)
combine_boot_and_kernel() {
  concat_image "$@"
}

zip_dir_contents() {
  local dir="$1" out_zip="$2"
  if ! command -v zip >/dev/null 2>&1; then
    die "'zip' not found in PATH"
  fi
  rm -f "$out_zip"
  (cd "$dir" && zip -rq "$out_zip" .)
}
