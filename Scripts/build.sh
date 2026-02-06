#!/usr/bin/env bash
# Xcode runs build scripts using /bin/sh by default. If this script is invoked
# as `sh build.sh ...`, re-exec under bash so the build system can use bash.
if [ -z "${BASH_VERSION:-}" ]; then
  exec /usr/bin/env bash "$0" "$@"
fi

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KERNEL_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
# Repository layout:
#   <repo>/Code/OS/Scripts/build.sh
# so the repo root is two levels above OS/.
REPO_ROOT="$(cd "${KERNEL_DIR}/../.." && pwd)"

source "${SCRIPT_DIR}/build_common.sh"

parse_args_common "$@"

# Default to aarch64-virt unless a platform flag is provided.
if [[ "${PLATFORM_EXPLICIT:-0}" != "1" ]]; then
  case "${PLATFORM:-}" in
    ""|macosx|iphoneos|iphonesimulator|appletvos|appletvsimulator|watchos|watchsimulator|xros|xrsimulator|driverkit|android|qnx|webassembly)
      PLATFORM="aarch64-virt"
      ;;
  esac
  log "No platform specified; defaulting to ${PLATFORM}"
fi

select_platform
preflight_common

# --- Housekeeping for repeat builds ---
#
# Xcode re-runs this build script on every build invocation. Our build scripts
# are intentionally simple (not incremental), and we bump the build number on
# every successful build. Keeping a previous OUT_DIR around can allow stale
# intermediates to leak into a later build. This is the root cause of “first
# build succeeds, second build fails until you delete build/”.
#
# We therefore default to wiping *this target's* OUT_DIR before building.
# This keeps other targets/configurations intact and avoids manual cleanup.
#
# To opt out (for experimentation with incremental behavior), set:
#   CAPAZ_CLEAN_OUT_DIR=0
if [[ "${CAPAZ_CLEAN_OUT_DIR:-1}" != "0" ]]; then
    case "${TARGET}" in
      both|"")
        for t in loader kernel_c; do
          tmp_out="${REPO_ROOT}/build/${PLATFORM}/${CONFIG}/${t}"
          if [[ "${tmp_out}" == "${REPO_ROOT}/build/"* ]]; then
            rm -rf "${tmp_out}"
          fi
        done
        ;;
      kernel|kernel_c|loader)
        if [[ -n "${OUT_DIR:-}" && "${OUT_DIR}" == "${REPO_ROOT}/build/"* ]]; then
          rm -rf "${OUT_DIR}"
        fi
        ;;
    esac
fi

# Choose a buildinfo.ini to read versioning from.
#
# IMPORTANT: by default we bump kernel_build_number on every build so buildinfo.h/.c
# reflect the current build. You can disable this behavior by exporting:
#   CAPAZ_BUMP_BUILD_NUMBER=0
if [ -z "${BUILDINFO_INI:-}" ]; then
  if [ -f "${KERNEL_DIR}/Scripts/buildinfo.ini" ]; then
    BUILDINFO_INI="${KERNEL_DIR}/Scripts/buildinfo.ini"
  elif [ -f "${KERNEL_DIR}/buildinfo.ini" ]; then
    BUILDINFO_INI="${KERNEL_DIR}/buildinfo.ini"
  elif [ -f "${REPO_ROOT}/buildinfo.ini" ]; then
    BUILDINFO_INI="${REPO_ROOT}/buildinfo.ini"
  else
    # No repo config found: generate a local default in the build folder so the
    # build can proceed without modifying the working tree.
    mkdirp "${OUT_DIR}/gen"
    BUILDINFO_INI="${OUT_DIR}/gen/buildinfo.ini"
    cat >"${BUILDINFO_INI}" <<'EOF'
# Build info for CapazOS

[build]
build_version=0.0.0
build_environment=macOS Xcode
build_date=

[kernel]
kernel_version=0.0.0
kernel_build_number=0
kernel_machine=unknown
kernel_config=Debug

[core]
core_name=Core
core_version=0.0.0
EOF
  fi
fi
export BUILDINFO_INI

# Ensure buildinfo.ini has the current machine (board) identifier.
# The board is selected via --virt/--platform and resolved in select_platform.
if [[ -n "${BOARD:-}" ]]; then
  set_buildinfo_value "${BUILDINFO_INI}" kernel_machine "${BOARD}"
fi

# Bump build number once per build invocation unless explicitly disabled.
if [[ "${CAPAZ_BUMP_BUILD_NUMBER:-1}" != "0" && "${ACTION:-build}" != "clean" ]]; then
  # Bump the kernel build number once per build invocation.
  "${SCRIPT_DIR}/bump_build_number.sh" --key kernel_build_number "${BUILDINFO_INI}" || true
fi

declare -a BUILT_OUT_DIRS=()
BUILT_KERNEL=0

# Dispatch by target.
case "${TARGET}" in
  both|"")
    TARGET="loader"
    OUT_DIR="${REPO_ROOT}/build/${PLATFORM}/${CONFIG}/${TARGET}"
    build_loader
    BUILT_OUT_DIRS+=("${OUT_DIR}")

    TARGET="kernel_c"
    OUT_DIR="${REPO_ROOT}/build/${PLATFORM}/${CONFIG}/${TARGET}"
    build_boot_and_kernel
    BUILT_OUT_DIRS+=("${OUT_DIR}")
    BUILT_KERNEL=1
    ;;
  loader)
    TARGET="loader"
    OUT_DIR="${REPO_ROOT}/build/${PLATFORM}/${CONFIG}/${TARGET}"
    build_loader
    BUILT_OUT_DIRS+=("${OUT_DIR}")
    ;;
  kernel|kernel_c)
    TARGET="kernel_c"
    OUT_DIR="${REPO_ROOT}/build/${PLATFORM}/${CONFIG}/${TARGET}"
    build_boot_and_kernel
    BUILT_OUT_DIRS+=("${OUT_DIR}")
    BUILT_KERNEL=1
    ;;
  *)
    die "Unknown target: ${TARGET}"
    ;;
esac

# --- Final artifact location ---
FINAL_BUILD_DIR="${REPO_ROOT}/build"
mkdirp "$FINAL_BUILD_DIR"

echo "note: Install build artifacts -> ${FINAL_BUILD_DIR}"

found_any=0
for build_dir in "${BUILT_OUT_DIRS[@]}"; do
  while IFS= read -r -d '' f; do
      found_any=1
      dest="${FINAL_BUILD_DIR}/$(basename "$f")"
      # Move artifacts up to build/ (overwrite if needed)
      if [ "$f" != "$dest" ]; then
          mv -f "$f" "$dest"
      fi
  done < <(find "$build_dir" -maxdepth 1 -type f \( -name '*.img' -o -name '*.elf' -o -name '*.bin' \) -print0)
done

if [ "$found_any" -eq 0 ]; then
    die "No build artifacts (*.img/*.elf/*.bin) found in built output directories"
fi

# kernel.img is still expected for run workflows (kernel build only)
if [[ "${BUILT_KERNEL}" == "1" ]]; then
    if [ ! -f "${FINAL_BUILD_DIR}/kernel.img" ]; then
        die "kernel.img not produced (expected ${FINAL_BUILD_DIR}/kernel.img)"
    fi

    echo "note: Archiving build + source"
    "${SCRIPT_DIR}/archive.sh" "$REPO_ROOT" "$FINAL_BUILD_DIR" "$BUILDINFO_INI"
fi
