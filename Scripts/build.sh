#!/usr/bin/env bash
# Xcode runs script phases using /bin/sh by default. If this script is invoked
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

# Enforce explicit platform selection for CLI builds.
# Use --virt or --platform <name>. To bypass (e.g. for experiments), set:
#   CAPAZ_ALLOW_IMPLICIT_PLATFORM=1
if [[ "${PLATFORM_EXPLICIT:-0}" != "1" && "${CAPAZ_ALLOW_IMPLICIT_PLATFORM:-0}" != "1" ]]; then
  die "Platform must be explicit. Use --virt or --platform <name>."
fi

select_platform
preflight_common

# --- Housekeeping for repeat builds ---
#
# Xcode re-runs this script phase on every build invocation. Our build scripts
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
    if [[ -n "${OUT_DIR:-}" && "${OUT_DIR}" == "${REPO_ROOT}/build/"* ]]; then
        rm -rf "${OUT_DIR}"
    fi
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
kernel_platform=unknown
kernel_machine=unknown
kernel_config=Debug

[core]
core_name=Core
core_version=0.0.0
EOF
  fi
fi
export BUILDINFO_INI

# Bump build number once per build invocation unless explicitly disabled.
if [[ "${CAPAZ_BUMP_BUILD_NUMBER:-1}" != "0" && "${ACTION:-build}" != "clean" ]]; then
  # Bump the kernel build number once per build invocation.
  "${SCRIPT_DIR}/bump_build_number.sh" --key kernel_build_number "${BUILDINFO_INI}" || true
fi

build_boot_and_kernel

# --- Final artifact location ---
FINAL_BUILD_DIR="${REPO_ROOT}/build"
mkdirp "$FINAL_BUILD_DIR"

echo "note: Install build artifacts -> ${FINAL_BUILD_DIR}"

found_any=0
while IFS= read -r -d '' f; do
    found_any=1
    dest="${FINAL_BUILD_DIR}/$(basename "$f")"
    # Move artifacts up to build/ (overwrite if needed)
    if [ "$f" != "$dest" ]; then
        mv -f "$f" "$dest"
    fi
done < <(find "$OUT_DIR" -maxdepth 1 -type f \( -name '*.img' -o -name '*.elf' -o -name '*.bin' \) -print0)

if [ "$found_any" -eq 0 ]; then
    die "No build artifacts (*.img/*.elf/*.bin) found in ${OUT_DIR}"
fi

# kernel.img is still expected for run workflows
if [ ! -f "${FINAL_BUILD_DIR}/kernel.img" ]; then
    die "kernel.img not produced (expected ${FINAL_BUILD_DIR}/kernel.img)"
fi

echo "note: Archiving build + source"
"${SCRIPT_DIR}/archive.sh" "$REPO_ROOT" "$FINAL_BUILD_DIR" "$BUILDINFO_INI"
