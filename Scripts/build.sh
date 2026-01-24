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
  -I"${KERNEL_DIR}/Sources/ABI"
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
  Kernel/Scripts/build.sh [--platform aarch64-virt] [--target boot|kernel_c|core_c|core_swift] [--with-core|--without-core] [--clean]

Notes:
  - CORE_MODE=auto|on|off controls optional Core(C) linking.
    --with-core sets CORE_MODE=on, --without-core sets CORE_MODE=off.
  - This script is the single entry point for both Xcode targets.
  - Platform modules live under Kernel/Scripts/platforms/.
  - Target modules live under Kernel/Scripts/targets/.
  - Build products are written to <Workspace>/build (sibling to Kernel/ and Core/).
USAGE
}

CLEAN=0
CORE_MODE=${CORE_MODE:-auto}  # auto|on|off
while [[ $# -gt 0 ]]; do
  case "$1" in
    --platform)
      PLATFORM="$2"; shift 2 ;;
    --target)
      TARGET="$2"; shift 2 ;;
    --with-core)
      CORE_MODE=on; shift ;;
    --without-core)
      CORE_MODE=off; shift ;;
    --clean)
      CLEAN=1; shift ;;
    -h|--help)
      usage; exit 0 ;;
    boot|kernel_c|core_c|core_swift)
      TARGET="$1"; shift ;;
    *)
      echo "Unknown argument: $1" >&2
      usage
      exit 2
      ;;
  esac
done

# ---------------------------------------------------------------------------
# Pre-build: archive previous build artifacts and then delete the build folder.
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

  # Support both spellings to avoid brittle builds.
  local kernel_img=""
  if [[ -f "$OUT_DIR/Kernel.img" ]]; then
    kernel_img="$OUT_DIR/Kernel.img"
  elif [[ -f "$OUT_DIR/kernel.img" ]]; then
    kernel_img="$OUT_DIR/kernel.img"
  else
    return 0
  fi

  ensure_dir "$ARCHIVE_DIR"

  # Archive both the source tree (Code/) and the built kernel image.
  local zip_path="$ARCHIVE_DIR/Code.${build_num}.zip"
  rm -f "$zip_path"

  local staging_dir
  staging_dir="$(mktemp -d "${TMPDIR:-/tmp}/capazos-archive.XXXXXX")"
  mkdir -p "$staging_dir"

  # Stage Code/ as Code/ (not an absolute path) and stage the kernel as Kernel.img.
  cp -a "$CODE_DIR" "$staging_dir/Code"
  cp -a "$kernel_img" "$staging_dir/Kernel.img"

  if ! command -v zip >/dev/null 2>&1; then
    die "'zip' is required to archive Code/ + Kernel.img (install zip or ensure it's in PATH)"
  fi

  (
    cd "$staging_dir"
    zip -q -r "$zip_path" "Code" "Kernel.img"
  )

  rm -rf "$staging_dir"
  log "Archived previous build -> $zip_path"
}

### Cleaning / archiving policy
#
# The kernel build produces a "kernel.img" in $OUT_DIR. Historically we always
# cleaned $OUT_DIR at the start of *every* invocation; that works for "kernel"
# builds, but it breaks the "Core"/"core_swift" target because Xcode will run
# the script and we end up archiving + deleting the last kernel image even
# though we're not rebuilding it.
#
# Policy:
#   - kernel_c: archive existing kernel.img, then clean the entire build dir.
#   - boot:     do NOT delete the whole build dir (boot doesn't produce kernel.img).
#   - core_*:   do NOT archive or delete the whole build dir; only clean the
#               core-specific outputs when --clean is passed.

case "$TARGET" in
  kernel_c|core_swift)
    archive_previous_kernel_img_if_present
    rm -rf "$OUT_DIR"
    ;;
  clean)
    rm -rf "$OUT_DIR"
    ;;
  *)
    # Keep existing build products (especially kernel.img).
    ;;
esac

# A dedicated clean target should not regenerate build info or recreate the
# build directory; it should just remove it and exit.
if [[ "$TARGET" == "clean" ]]; then
  echo "Cleaned -> $OUT_DIR"
  exit 0
fi

# Ensure output dirs exist.
ensure_dir "$OUT_DIR"
ensure_dir "$OBJ_DIR"
ensure_dir "$OUT_DIR/include"

# Optional clean flag: remove known artifacts, but do not nuke the entire build
# dir unless the target is "kernel_c" or "clean" (handled above).
if [[ "$CLEAN" == "1" ]]; then
  case "$TARGET" in
    core_swift)
      rm -f "$OBJ_DIR/core_swift.o" "$OUT_DIR/core_swift.o" || true
      ;;
    core_c)
      rm -f "$OBJ_DIR/core_c.o" "$OUT_DIR/core_c.o" || true
      ;;
    boot)
      rm -f "$OUT_DIR/boot.elf" "$OUT_DIR/boot.bin" || true
      ;;
    kernel_c)
      # kernel_c already cleared $OUT_DIR above.
      ;;
    *)
      # Conservative: only clear obj dir.
      rm -rf "$OBJ_DIR" || true
      ensure_dir "$OBJ_DIR"
      ;;
  esac
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
  core_c)
    # shellcheck source=targets/core_c.sh
    source "$SCRIPTS_DIR/targets/core_c.sh"
    build_core_c "$OUT_DIR" "$OBJ_DIR" "$KERNEL_DIR" "$CORE_DIR"
    rc=$?
    if [ $rc -ne 0 ]; then
      if [ $rc -eq 1 ]; then
        die "No Core sources found under: $CORE_DIR/Sources"
      else
        die "Core(C) compilation failed."
      fi
    fi
    ;;
  core_swift)
    # Build full Kernel image with Swift Core objects linked in.
    # shellcheck source=targets/boot.sh
    source "$SCRIPTS_DIR/targets/boot.sh"
    # shellcheck source=targets/core_swift.sh
    source "$SCRIPTS_DIR/targets/core_swift.sh"
      # shellcheck source=targets/core_c.sh
      source "$SCRIPTS_DIR/targets/core_c.sh"
    # shellcheck source=targets/kernel_c.sh
    source "$SCRIPTS_DIR/targets/kernel_c.sh"

      # Force Core on for this target (Swift + Core C stubs).
    CORE_MODE="swift"
      export CORE_SWIFT_OBJ="$OBJ_DIR/core_swift.o"
      # build_core_c publishes to $OUT_DIR/core_c.o
      export CORE_C_OBJ="$OUT_DIR/core_c.o"

    build_boot "$OUT_DIR" "$OBJ_DIR" "$KERNEL_DIR"
    build_core_swift "$OUT_DIR" "$OBJ_DIR" "$CORE_DIR"
    build_core_c "$OUT_DIR" "$OBJ_DIR" "$KERNEL_DIR" "$CORE_DIR"
    rc=$?
    if [ $rc -ne 0 ]; then
      if [ $rc -eq 1 ]; then
        die "No Core sources found under: $CORE_DIR/Sources"
      else
        die "Core(C) compilation failed."
      fi
    fi
    build_kernel_c "$OUT_DIR" "$OBJ_DIR" "$KERNEL_DIR"
    ;;
  *)
    die "Unknown target: $TARGET"
    ;;
esac
