#!/usr/bin/env bash
set -euo pipefail

# Swift compilation helpers (Option 2: pure-script Swift build).
#
# Sourced by OS/Scripts/build_common.sh when present.
# Provides `swift_embed_compile`, which compiles Swift sources into a single
# object file suitable for linking into the kernel image.
#
# Notes:
#   - This is intentionally minimal and mirrors the earlier bring-up approach:
#     compile Swift into one .o and link it into kernel.elf.
#   - Runtime support is handled separately by the kernel (e.g.
#     OS/Core/swift_runtime_shims.c).

_swift_find_swiftc() {
  if [[ -n "${SWIFTC:-}" ]]; then
    echo "${SWIFTC}"
    return 0
  fi
  if command -v swiftc >/dev/null 2>&1; then
    echo "swiftc"
    return 0
  fi
  return 1
}

swift_embed_compile() {
  local out_obj="$1"; shift

  if [[ $# -lt 1 ]]; then
    echo "swift_embed_compile: no input sources" >&2
    return 2
  fi

  local swiftc
  if ! swiftc="$(_swift_find_swiftc)"; then
    echo "swift_embed_compile: SWIFTC not set and swiftc not found on PATH" >&2
    return 2
  fi

  mkdir -p "$(dirname "${out_obj}")"

  # Target triple matches the historical build systems.
  local target="${SWIFT_TARGET_TRIPLE:-aarch64-none-none-elf}"

  # Common embedded Swift flags.
  # Allow override via SWIFTFLAGS_COMMON (string) and SWIFTFLAGS_EXTRA (array-like string).
  local -a args=()
  args+=("-target" "${target}")
  args+=("-emit-object")
  args+=("-parse-as-library")
  args+=("-wmo")
  # Embedded mode.
  # Newer Swift snapshots require enabling the Embedded experimental feature
  # rather than passing a bare "Embedded" token to the frontend.
  args+=("-Xfrontend" "-enable-experimental-feature")
  args+=("-Xfrontend" "Embedded")
  # Keep parity with prior bring-up scripts.
  args+=("-Xfrontend" "-disable-stack-protector")

  # Allow the caller/toolchain to inject additional flags.
  if [[ -n "${SWIFTFLAGS_COMMON:-}" ]]; then
    # shellcheck disable=SC2206
    args+=(${SWIFTFLAGS_COMMON})
  fi
  if [[ -n "${SWIFTFLAGS_EXTRA:-}" ]]; then
    # shellcheck disable=SC2206
    args+=(${SWIFTFLAGS_EXTRA})
  fi

  args+=("-o" "${out_obj}")

  echo "[swift] ${swiftc} ${args[*]} ${*}" >&2
  "${swiftc}" "${args[@]}" "$@"
}
