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
preflight_common

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
# The kernel build produces kernel.img inside the selected OUT_DIR (which varies
# by arch/config). For easier debugging/run workflows, always copy the final
# kernel image into <repo>/build/kernel.img.
SRC_KERNEL_IMG="${OUT_DIR}/kernel.img"
FINAL_BUILD_DIR="${REPO_ROOT}/build"
FINAL_KERNEL_IMG="${FINAL_BUILD_DIR}/kernel.img"

resolve_path() {
    local p="$1"
    printf '%s\n' "$(cd "$(dirname "${p}")" && pwd -P)/$(basename "${p}")"
}

if [ -f "${SRC_KERNEL_IMG}" ]; then
    mkdirp "${FINAL_BUILD_DIR}"
    if [ "$(resolve_path "${SRC_KERNEL_IMG}")" != "$(resolve_path "${FINAL_KERNEL_IMG}")" ]; then
        /bin/cp -f "${SRC_KERNEL_IMG}" "${FINAL_KERNEL_IMG}"
    fi
fi

# --- Archiving ---
# Keep this minimal: after a successful build, archive CapazOS/Code and the
# final kernel image into CapazOS/archive/OS.<kernel_build_number>.zip.
if [ -f "${FINAL_KERNEL_IMG}" ] && [[ "${ACTION:-build}" != "clean" ]]; then
    "${SCRIPT_DIR}/archive.sh" "${REPO_ROOT}" "${FINAL_KERNEL_IMG}" "${BUILDINFO_INI}"
fi
