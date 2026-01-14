#!/usr/bin/env bash
set -euo pipefail

# Unified build entry point (invoked by Xcode run script).
#
# Assumptions (by design):
#   <Workspace>/Kernel   (this repo)
#   <Workspace>/Core     (sibling folder)
#   <Workspace>/build    (build outputs)
#   <Workspace>/archive  (archived kernel images)

THIS="${BASH_SOURCE[0]}"
SCRIPTS_DIR="$(cd "$(dirname "$THIS")" && pwd)"
KERNEL_DIR="$(cd "${SCRIPTS_DIR}/.." && pwd)"
WORKSPACE_DIR="$(cd "${KERNEL_DIR}/.." && pwd)"

# Load toolchain.
# shellcheck source=toolchain.env
source "${SCRIPTS_DIR}/toolchain.env"

# shellcheck source=lib/common.sh
source "${SCRIPTS_DIR}/lib/common.sh"

# shellcheck source=lib/buildinfo.sh
source "${SCRIPTS_DIR}/lib/buildinfo.sh"

# Default platform + target
PLATFORM="aarch64-virt"
TARGET="kernel_c"

# Workspace layout
# - Source tree lives in CODE_DIR (typically <workspace>/Code).
# - Build products are emitted to WORKSPACE_ROOT (typically <workspace>).
CODE_DIR="${WORKSPACE_DIR:-$(cd "${KERNEL_DIR}/.." && pwd)}"
WORKSPACE_DIR="${CODE_DIR}"
if [[ "$(basename "${CODE_DIR}")" == "Code" ]]; then
  WORKSPACE_ROOT="$(cd "${CODE_DIR}/.." && pwd)"
else
  WORKSPACE_ROOT="${CODE_DIR}"
fi




# Build artifacts live in the workspace root (sibling to Kernel/ and Core/).
OUT_DIR="${WORKSPACE_ROOT}/build"
OBJ_DIR="${OUT_DIR}/obj"

ARCHIVE_DIR="${WORKSPACE_ROOT}/archive"
BUILDINFO_INI="${SCRIPTS_DIR}/buildinfo.ini"
BUILDINFO_HEADER="${OUT_DIR}/include/build_info.h"

CORE_DIR="${CODE_DIR}/Core"

# ---------------------------------------------------------------------------
# Shared compiler flags (targets consume BOOT_CFLAGS / KERNEL_CFLAGS).
# ---------------------------------------------------------------------------
COMMON_CFLAGS=(
  -Wall -Wextra -Werror
  -ffreestanding
  -fno-builtin
  -fno-common
  -fno-omit-frame-pointer
  -fno-stack-protector
  -fno-pic
  -fno-pie
  -O2
  -g
  -mcpu=cortex-a72
  -mstrict-align
  -target aarch64-none-elf
  # Kernel headers live alongside sources (no dedicated Include/ dir).
  # Keep these broad so "#include \"uart_pl011.h\"" and friends resolve.
  -I"${KERNEL_DIR}/Sources"
  -I"${KERNEL_DIR}/Sources/Kernel"
  -I"${KERNEL_DIR}/Sources/Kernel/boot"
  -I"${KERNEL_DIR}/Sources/Kernel/mm"
  -I"${KERNEL_DIR}/Sources/Kernel/sched"
  -I"${KERNEL_DIR}/Sources/Kernel/platform"
  -I"${KERNEL_DIR}/Sources/Kernel/debug"
  -I"${KERNEL_DIR}/Sources/Kernel/util"
  -I"${KERNEL_DIR}/Sources/Kernel/irg"
  -I"${KERNEL_DIR}/Sources/HAL"
  -I"${KERNEL_DIR}/Sources/Arch"
  -I"${OUT_DIR}/include"
)
BOOT_CFLAGS=("${COMMON_CFLAGS[@]}" -DBOOT_STAGE=1)
KERNEL_CFLAGS=("${COMMON_CFLAGS[@]}" -DKERNEL_STAGE=1)

usage() {
  cat <<USAGE
Usage:
  Kernel/Scripts/build.sh [--platform aarch64-virt] [--target boot|kernel_c|core_swift] [--clean]

Notes:
  - This script is the single entry point for both Xcode targets.
  - Platform modules live under Kernel/Scripts/platforms/.
  - Target modules live under Kernel/Scripts/targets/.
  - Build products are written to <Workspace>/build (sibling to Kernel/ and Core/).
USAGE
}

CLEAN=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --platform)
      PLATFORM="$2"; shift 2 ;;
    --target)
      TARGET="$2"; shift 2 ;;
    --clean)
      CLEAN=1; shift ;;
    -h|--help)
      usage; exit 0 ;;
    boot|kernel_c|core_swift)
      TARGET="$1"; shift ;;
    *)
      echo "Unknown argument: $1" >&2
      usage
      exit 2
      ;;
  esac
done

# ---------------------------------------------------------------------------
# Pre-build: archive previous kernel.img and then delete the build folder.
# ---------------------------------------------------------------------------

archive_previous_kernel_img_if_present() {
  local build_num=0

  # Read previous build number (source of truth).
  if [[ -f "$BUILDINFO_INI" ]]; then
    local n
    n="$(buildinfo__read_kv "$BUILDINFO_INI" "build_number")"
    if [[ "${n:-}" =~ ^[0-9]+$ ]]; then
      build_num="$n"
    fi
  fi

  local kernel_img="$OUT_DIR/kernel.img"
  if [[ ! -f "$kernel_img" ]]; then
    return 0
  fi

  ensure_dir "$ARCHIVE_DIR"

  local zip_path="$ARCHIVE_DIR/Kernel.${build_num}.zip"
  rm -f "$zip_path"

  if command -v zip >/dev/null 2>&1; then
    # -j: junk paths (store only filename), -q: quiet
    zip -q -j "$zip_path" "$kernel_img"
  elif command -v ditto >/dev/null 2>&1; then
    # macOS-friendly fallback
    ditto -c -k --sequesterRsrc --keepParent "$kernel_img" "$zip_path"
  else
    die "Neither 'zip' nor 'ditto' is available to archive kernel.img"
  fi

  log "Archived previous kernel image -> $zip_path"
}

archive_previous_kernel_img_if_present
rm -rf "$OUT_DIR"

# Recreate output dirs after cleaning.
ensure_dir "$OUT_DIR"
ensure_dir "$OBJ_DIR"
ensure_dir "$OUT_DIR/include"

# Optional clean flag: treat as "extra clean" on top of the always-clean behavior.
if [[ "$CLEAN" == "1" ]]; then
  rm -rf "$OBJ_DIR"     "$OUT_DIR/boot.elf" "$OUT_DIR/boot.bin"     "$OUT_DIR/kernel.elf" "$OUT_DIR/kernel.bin" "$OUT_DIR/kernel.img"     "$OUT_DIR/Kernel.zip" || true
  ensure_dir "$OBJ_DIR"
fi

# ---------------------------------------------------------------------------
# Load platform defaults.
# ---------------------------------------------------------------------------

PLATFORM_FILE="$SCRIPTS_DIR/platforms/${PLATFORM}.sh"
if [[ ! -f "$PLATFORM_FILE" ]]; then
  die "Platform not found: $PLATFORM_FILE"
fi
# shellcheck source=/dev/null
source "$PLATFORM_FILE"

# ---------------------------------------------------------------------------
# Build metadata (increments build number and generates build_info.h)
# ---------------------------------------------------------------------------

# IMPORTANT: pass FILE paths (not directories).
generate_build_info "$BUILDINFO_INI" "$BUILDINFO_HEADER"

# ---------------------------------------------------------------------------
# Dispatch to target modules.
# ---------------------------------------------------------------------------

case "$TARGET" in
  boot)
    # shellcheck source=targets/boot.sh
    source "$SCRIPTS_DIR/targets/boot.sh"
    build_boot "$OUT_DIR" "$OBJ_DIR" "$KERNEL_DIR"
    ;;
  kernel_c)
    # shellcheck source=targets/kernel_c.sh
    source "$SCRIPTS_DIR/targets/kernel_c.sh"
    build_kernel_c "$OUT_DIR" "$OBJ_DIR" "$KERNEL_DIR"
    ;;
  core_swift)
    # shellcheck source=targets/core_swift.sh
    source "$SCRIPTS_DIR/targets/core_swift.sh"
    build_core_swift "$OUT_DIR" "$OBJ_DIR" "$CORE_DIR"
    ;;
  *)
    die "Unknown target: $TARGET"
    ;;
esac
