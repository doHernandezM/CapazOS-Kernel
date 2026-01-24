#!/usr/bin/env bash
set -euo pipefail

TARGET_THIS="${BASH_SOURCE[0]}"
TARGET_DIR="$(cd "$(dirname "$TARGET_THIS")" && pwd)"

# shellcheck source=../lib/swift.sh
source "${TARGET_DIR}/../lib/swift.sh"
# shellcheck source=../lib/link.sh
source "${TARGET_DIR}/../lib/link.sh"

build_core_swift() {
  local out_dir="$1" obj_dir="$2" core_dir="$3"

  ensure_dir "$obj_dir"

  # Collect Swift sources (Bash 3.x compatible: avoid mapfile/readarray)
  local swift_files=()
  while IFS= read -r f; do
    swift_files+=("$f")
  done < <(find "$core_dir/Sources" -type f -name '*.swift' | sort)
  if [[ ${#swift_files[@]} -eq 0 ]]; then
    die "No Swift sources found under $core_dir/Sources"
  fi

  local out_obj="$obj_dir/core_swift.o"

  # Compile all Swift sources into a single object.
  # Use -parse-as-library since there is no 'main'.
  # Keep the Swift target triple aligned with the C toolchain.
  "$SWIFTC" \
    -target aarch64-none-none-elf \
    -emit-object -parse-as-library -wmo \
    -Xfrontend -enable-experimental-feature \
    -Xfrontend Embedded \
    -Xfrontend -disable-stack-protector \
    "${swift_files[@]}" \
    -o "$out_obj"

  echo "[core_swift] built $out_obj"

  # Publish an easy-to-find build product at the build root.
  # (The object also remains in build/obj for incremental builds.)
  cp -f "$out_obj" "$out_dir/core_swift.o"
  echo "[core_swift] published $out_dir/core_swift.o"
}
