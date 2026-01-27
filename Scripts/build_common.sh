#!/usr/bin/env bash
set -euo pipefail

# ----- Safe defaults (must come before any other references under `set -u`) -----
: "${PLATFORM_NAME:=}"
: "${PLATFORM:="${PLATFORM_NAME:-}"}"

: "${CONFIGURATION:=}"
: "${PROJECT_DIR:=}"
: "${SRCROOT:=}"

# Provide a stable scripts dir variable for other scripts to use.
: "${SCRIPTS_DIR:="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)"}"
: "${SCRIPT_DIR:="${SCRIPTS_DIR}"}"

# Kernel root (Code/Kernel). Prefer SRCROOT when provided by Xcode, otherwise derive from Scripts/.
: "${KERNEL_DIR:="${SRCROOT:-$(cd "${SCRIPTS_DIR}/.." && pwd)}"}"
# Repo root (<CapazOS>/). We keep build artifacts out of the source tree.
#
# Typical layouts:
#   <CapazOS>/Code/Kernel
#   <CapazOS>/Code (Xcode WORKSPACE_DIR)
if [[ -z "${REPO_ROOT:-}" ]]; then
  if [[ -n "${WORKSPACE_DIR:-}" && "$(basename "${WORKSPACE_DIR}")" == "Code" ]]; then
    REPO_ROOT="$(cd "${WORKSPACE_DIR}/.." && pwd)"
  else
    # KERNEL_DIR is .../Code/Kernel => go up two
    REPO_ROOT="$(cd "${KERNEL_DIR}/../.." && pwd)"
  fi
fi

# Configuration defaults (can be overridden by parse_args or Xcode CONFIGURATION).
: "${PLATFORM:=${PLATFORM_NAME:-${PLATFORM:-}}}"
: "${PLATFORM:=aarch64-virt}"
: "${CONFIG:=${CONFIGURATION:-debug}}"

# Optional extra flags for link steps (arrays are safer under `set -u`).
declare -a LINK_EXTRA_FLAGS=()

# Toolchain selection (source once if present).
if [[ -z "${CAPAZ_TOOLCHAIN_SOURCED:-}" ]]; then
  if [[ -f "${SCRIPTS_DIR}/toolchain.env" ]]; then
    # shellcheck source=/dev/null
    source "${SCRIPTS_DIR}/toolchain.env"
  fi
  export CAPAZ_TOOLCHAIN_SOURCED=1
fi

# Default target if not specified by args
: "${TARGET:=kernel_c}"

# Flags can come from toolchain.env; we set safe defaults
CFLAGS_COMMON="${CFLAGS_COMMON:-}"
ASFLAGS_COMMON="${ASFLAGS_COMMON:-}"
LDFLAGS_COMMON="${LDFLAGS_COMMON:-}"
SWIFTFLAGS_COMMON="${SWIFTFLAGS_COMMON:-}"

# Canonical flag storage (arrays). This avoids subtle bugs where a whole
# flag string accidentally gets quoted and passed as a single "filename"
# argument to clang/ld.
declare -a CFLAGS_COMMON_ARR=()
declare -a ASFLAGS_COMMON_ARR=()
declare -a LDFLAGS_COMMON_ARR=()

# Optional include flags used by compilation commands.
#
# Some build paths (notably assembly) may want to inject additional include
# directories or preprocessor options. Under `set -u`, referencing an unset
# variable is a hard error, so define this as an empty array by default.
declare -a INCLUDE_FLAGS=()

# Build metadata generator (buildinfo.h/buildinfo.c).
source "${SCRIPT_DIR}/buildinfo.sh"


usage() {
  cat <<EOF
Usage: build.sh [options]

Options:
  --platform <name>        (default: aarch64-virt)
  --config <debug|release> (default: debug)
  --target <kernel_c|core> (default: kernel_c)
  --buildinfo-ini <path>   (default: Kernel/Scripts/buildinfo.ini)
  --out <dir>              (override OUT_DIR)
  -h, --help               show help
EOF
}


parse_args() {
  while [[ $# -gt 0 ]]; do
    case "$1" in
    "" ) shift ;;
      --platform)
        PLATFORM="${2:?missing value for --platform}"
        shift 2
        ;;
      --config)
        CONFIG="${2:?missing value for --config}"
        shift 2
        ;;
      --target)
        TARGET="${2:?missing value for --target}"
        shift 2
        ;;
      --buildinfo-ini)
        BUILDINFO_INI="${2:?missing value for --buildinfo-ini}"
        shift 2
        ;;
      --out)
        OUT_DIR="${2:?missing value for --out}"
        shift 2
        ;;
      -h|--help)
        usage
        exit 0
        ;;
      *)
        die "Unknown argument: $1"
        ;;
    esac
  done
}

# Backwards-compatible name used by some scripts
parse_args_common() {
  parse_args "$@"
}


# --- Shared helpers ----------------------------------------------------------

die() {
  echo "error: $*" >&2
  exit 1
}

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || die "missing required tool: $1"
}

mkdirp() {
  mkdir -p "$1"
}

realpath_fallback() {
  # macOS has realpath, but keep a fallback.
  if command -v realpath >/dev/null 2>&1; then
    realpath "$1"
  else
    python3 - <<'PY' "$1"
import os,sys
print(os.path.realpath(sys.argv[1]))
PY
  fi
}

# Deterministic discovery helpers ------------------------------------------------
#
list_sources_sorted() {
  # Usage:
  #   list_sources_sorted <dir...> -- <find predicates...>
  # Example:
  #   list_sources_sorted "$KERNEL_DIR/Sources/Kernel" "$KERNEL_DIR/Sources/HAL" -- \
  #     -type f \( -name "*.c" -o -name "*.S" -o -name "*.s" \)
  local dirs=()
  while [[ $# -gt 0 ]]; do
    if [[ "$1" == "--" ]]; then
      shift
      break
    fi
    dirs+=("$1")
    shift
  done
  [[ ${#dirs[@]} -gt 0 ]] || die "list_sources_sorted: missing dirs"
  # Remaining args are find predicates.
  find "${dirs[@]}" "$@" -print | LC_ALL=C sort
}

list_files_sorted0() {
  # Usage:
  #   list_files_sorted0 <dir> <name_pattern>
  # Example:
  #   list_files_sorted0 "$obj_dir" "*.o"
  local dir="$1"
  local pat="$2"
  find "${dir}" -type f -name "${pat}" -print0 | LC_ALL=C sort -z
}

# Preflight error aggregation ---------------------------------------------------
preflight_fail() {
  # Print a single actionable failure message.
  echo "error: preflight failed:" >&2
  local msg
  for msg in "$@"; do
    echo "  - ${msg}" >&2
  done
  exit 1
}

boundary_lint_core() {
  local core_src="${REPO_ROOT}/Code/Core/Sources"
  [[ -d "${core_src}" ]] || return 0
  # Disallow reaching into Kernel via path-based includes. ABI headers must be
  # included by basename with -I Kernel/Sources/ABI.
  if grep -R --line-number -E '#include[[:space:]]+["<](\.\./)*Kernel/' "${core_src}" >/dev/null 2>&1; then
    echo "Core includes kernel-private headers via a Kernel/ path (only ABI basenames via -I Kernel/Sources/ABI are allowed)."
    return 1
  fi
  if grep -R --line-number -E '#include[[:space:]]+<Kernel/' "${core_src}" >/dev/null 2>&1; then
    echo "Core includes kernel-private headers via <Kernel/...> (only ABI basenames via -I Kernel/Sources/ABI are allowed)."
    return 1
  fi
  return 0
}

boundary_lint_kernel() {
  local kernel_src="${KERNEL_DIR}/Sources"
  [[ -d "${kernel_src}" ]] || return 0
  # Kernel must not include Core headers. The only allowed cross-boundary header
  # is Kernel/Sources/ABI/*, which includes core_entrypoints.h (by basename).
  if grep -R --line-number -E '#include[[:space:]]+["<](\.\./)*Core/' "${kernel_src}" >/dev/null 2>&1; then
    echo "Kernel includes Core headers via a Core/ path (forbidden)."
    return 1
  fi
  return 0
}

gate_checks() {
  local errs=()

  # W^X selftest must be invoked during MMU init (not just defined).
  local mmu_file="${KERNEL_DIR}/Sources/Kernel/mm/mmu.c"
  if [[ -f "${mmu_file}" ]]; then
    if ! grep -q 'mmu_assert_layout_and_wx();' "${mmu_file}"; then
      errs+=("W^X selftest call missing: expected mmu_assert_layout_and_wx() invocation in Sources/Kernel/mm/mmu.c")
    fi
  else
    errs+=("Missing file: Sources/Kernel/mm/mmu.c (expected for W^X gate)")
  fi

  # Allocation must deny IRQ context.
  local kheap_file="${KERNEL_DIR}/Sources/Kernel/kheap.c"
  if [[ -f "${kheap_file}" ]]; then
    if ! grep -q 'ASSERT_THREAD_CONTEXT' "${kheap_file}"; then
      errs+=("IRQ allocation gate missing: expected ASSERT_THREAD_CONTEXT in Sources/Kernel/kheap.c")
    fi
  else
    errs+=("Missing file: Sources/Kernel/kheap.c (expected for allocation gate)")
  fi

  if (( ${#errs[@]} )); then
    preflight_fail "${errs[@]}"
  fi
}


# --- Preflight / toolchain ---------------------------------------------------

preflight_common() {
  # ... existing preflight code ...

  # Xcode exports SDKROOT and related vars for host builds; for a freestanding
  # cross build these can cause confusing sysroot warnings.
  unset SDKROOT SDK_DIR SDK_NAME

  # Defaults for Xcode Run Script environments.
  # (Scripts run with `set -u`, so missing exports would hard-fail.)
  : "${PLATFORM:=aarch64-virt}"
  : "${CONFIG:=${CONFIGURATION:-debug}}"
  CONFIG="${CONFIG}"
  # Place all build artifacts under the repository-level build/ directory.
  # This keeps paths stable and matches Xcode's expected DerivedData workflow.
  : "${BUILD_ROOT:=${REPO_ROOT}/build}"
  : "${OUT_DIR:=${REPO_ROOT}/build/${PLATFORM}/${CONFIG}/${TARGET}}"

  # ---------------------------------------------------------------------------
  # Force freestanding ELF codegen.
  #
  # If clang is invoked from Xcode with a global toolchain override, it can
  # default to an Apple platform target (shown as "XR" in warnings). That puts
  # the assembler in Mach-O mode and it will reject ELF/GAS directives like
  # `.type` / `.size` used in our AArch64 assembly.
  #
  # By setting an explicit ELF triple we ensure:
  #   - assembly accepts `.type` / `.size`
  #   - produced objects are ELF, matching ld.lld + our linker scripts
  # ---------------------------------------------------------------------------
  if [[ -z "${TARGET_TRIPLE:-}" ]]; then
    case "${PLATFORM:-}" in
      aarch64-*|arm64-*)
        TARGET_TRIPLE="aarch64-none-none-elf"
        ;;
      *)
        # Default to aarch64-none-none-elf unless you add more platforms.
        TARGET_TRIPLE="aarch64-none-none-elf"
        ;;
    esac
  fi

  # Common target flags for both C and ASM compilation.
  TARGET_FLAGS=(
    -target "${TARGET_TRIPLE}"
    -ffreestanding
    -fno-builtin
    -fno-pic
    -fno-pie
  )

  # We want Clang's *builtin* headers (e.g. <stdint.h>) even for freestanding
  # builds. Do NOT use -nostdinc, because it disables the builtin header search
  # paths too.

  # In some toolchains, Clang's builtin include directory isn't added in all
  # contexts (e.g. custom -target). Add it explicitly so basic headers resolve.
  local _clang_resdir
  _clang_resdir="$(${CC} -print-resource-dir 2>/dev/null || true)"
  if [[ -n "${_clang_resdir}" && -d "${_clang_resdir}/include" ]]; then
    # Use the "${var:-}" pattern first to avoid set -u failures if the vars are
    # currently unset.
    CFLAGS_COMMON="${CFLAGS_COMMON:-} -isystem ${_clang_resdir}/include"
    ASFLAGS_COMMON="${ASFLAGS_COMMON:-} -isystem ${_clang_resdir}/include"
  fi

  # Build canonical flag arrays. We accept any user/toolchain.env-provided
  # strings, but we always append a safe set of defaults for a freestanding
  # kernel build.
  CFLAGS_COMMON_ARR=()
  ASFLAGS_COMMON_ARR=()
  LDFLAGS_COMMON_ARR=()

  if [[ -n "${CFLAGS_COMMON:-}" ]]; then
    read -r -a _tmp <<< "${CFLAGS_COMMON}"
    CFLAGS_COMMON_ARR+=("${_tmp[@]}")
  fi
  if [[ -n "${ASFLAGS_COMMON:-}" ]]; then
    read -r -a _tmp <<< "${ASFLAGS_COMMON}"
    ASFLAGS_COMMON_ARR+=("${_tmp[@]}")
  fi
  if [[ -n "${LDFLAGS_COMMON:-}" ]]; then
    read -r -a _tmp <<< "${LDFLAGS_COMMON}"
    LDFLAGS_COMMON_ARR+=("${_tmp[@]}")
  else
    # Sensible defaults for a statically-linked freestanding kernel image.
    LDFLAGS_COMMON_ARR+=( -nostdlib -static -z notext -z max-page-size=4096 )
  fi

  # Ensure the build uses these even if toolchain.env didn't set them.
  CFLAGS_COMMON_ARR+=(
    "${TARGET_FLAGS[@]}"
    -ffreestanding
    -fno-builtin
    -fno-pic
    -fno-pie
    -fno-stack-protector
    -nostdinc
    -D__CAPAZ_KERNEL__=1
    -O2 -g
    -Wall -Wextra
    -Werror=implicit-function-declaration
    -Wno-unused-parameter
    -Wno-gnu-statement-expression
    -Wno-gnu-offsetof-extensions
    -Wno-address-of-packed-member
    -Wno-c99-designator
    -Wno-c11-extensions
    -Wno-c23-extensions
    -Wno-unused-command-line-argument
  )

  ASFLAGS_COMMON_ARR+=(
    "${TARGET_FLAGS[@]}"
    -ffreestanding
    -fno-builtin
    -fno-pic
    -fno-pie
    -fno-stack-protector
    -nostdinc
    -D__CAPAZ_KERNEL__=1
    -O2 -g
    -Wall -Wextra
    -Wno-unused-command-line-argument
  )

  # Keep legacy string forms for logging/debug output.
  CFLAGS_COMMON="${CFLAGS_COMMON_ARR[*]}"
  ASFLAGS_COMMON="${ASFLAGS_COMMON_ARR[*]}"
  LDFLAGS_COMMON="${LDFLAGS_COMMON_ARR[*]}"


  
# Toolchain + target-specific checklist (single failure message).
local errs=()

[[ -n "${CC:-}" ]] || errs+=("CC not set (expected via Kernel/Scripts/toolchain.env)")
[[ -n "${LD:-}" ]] || errs+=("LD not set (expected via Kernel/Scripts/toolchain.env)")
[[ -n "${OBJCOPY:-}" ]] || errs+=("OBJCOPY not set (expected via Kernel/Scripts/toolchain.env)")

# AR isn't always provided by Xcode / toolchain.env; choose a reasonable default.
if [[ -z "${AR:-}" ]]; then
  if command -v llvm-ar >/dev/null 2>&1; then
    AR="llvm-ar"
  elif command -v ar >/dev/null 2>&1; then
    AR="ar"
  else
    errs+=("AR not set and neither llvm-ar nor ar found in PATH")
  fi
fi

# Required directories / files per target.
if [[ "${TARGET}" == "kernel_c" || "${TARGET}" == "core" ]]; then
  [[ -d "${KERNEL_DIR}/Sources/Kernel" ]] || errs+=("Missing dir: Kernel/Sources/Kernel")
  [[ -d "${KERNEL_DIR}/Sources/HAL" ]] || errs+=("Missing dir: Kernel/Sources/HAL")
  [[ -d "${KERNEL_DIR}/Sources/Arch/aarch64" ]] || errs+=("Missing dir: Kernel/Sources/Arch/aarch64")
  [[ -f "${KERNEL_DIR}/Linker/kernel.ld" ]] || errs+=("Missing file: Kernel/Linker/kernel.ld")
  [[ -f "${KERNEL_DIR}/Linker/boot.ld" ]] || errs+=("Missing file: Kernel/Linker/boot.ld")
  [[ -d "${KERNEL_DIR}/Sources/ABI" ]] || errs+=("Missing dir: Kernel/Sources/ABI")
fi

if [[ "${TARGET}" == "core" ]]; then
  [[ -d "${REPO_ROOT}/Code/Core/Sources" ]] || errs+=("Missing dir: Code/Core/Sources (required for --target core)")
  [[ -f "${REPO_ROOT}/Code/Core/Scripts/build_core.sh" ]] || errs+=("Missing script: Code/Core/Scripts/build_core.sh")
fi

# Boundary lint gates
if [[ "${TARGET}" == "core" ]]; then
  if ! boundary_lint_core >/dev/null 2>&1; then
    errs+=("Core boundary lint failed (Core must not include Kernel-private headers; only ABI basenames are allowed).")
  fi
fi
if ! boundary_lint_kernel >/dev/null 2>&1; then
  errs+=("Kernel boundary lint failed (Kernel must not include Core headers).")
fi

if (( ${#errs[@]} )); then
  preflight_fail "${errs[@]}"
fi

# Tool presence (after values are validated).
need_cmd "${CC}"
need_cmd "${LD}"
need_cmd "${OBJCOPY}"
need_cmd "${AR}"
if [[ -n "${NM:-}" ]]; then need_cmd "${NM}"; fi

# drift-prevention gates (cheap checks).
gate_checks
}


# --- Build info --------------------------------------------------------------

emit_buildinfo_header() {
  # Generates:
  #   <gen_include_dir>/buildinfo.h   (canonical, assembler-safe)
  #   <gen_dir>/buildinfo.c          (optional but useful)
  local gen_include_dir="$1"
  local gen_dir
  gen_dir="$(cd "${gen_include_dir}/.." && pwd)"

  mkdirp "${gen_include_dir}"
  mkdirp "${gen_dir}"

  # Single source-of-truth INI for versioning.
  # Default location is always Kernel/Scripts/buildinfo.ini.
  # If an older build left buildinfo.ini elsewhere, we *migrate* it back.
  local ini="${BUILDINFO_INI:-}"
  local scripts_ini="${KERNEL_DIR}/Scripts/buildinfo.ini"

  if [[ -z "${ini}" ]]; then
    ini="${scripts_ini}"

    if [[ -f "${scripts_ini}" ]]; then
      : # already in the right place

    elif [[ -f "${KERNEL_DIR}/buildinfo.ini" ]]; then
      # Migrate legacy location -> Scripts/
      mkdirp "${KERNEL_DIR}/Scripts"
      if cp -n "${KERNEL_DIR}/buildinfo.ini" "${scripts_ini}" 2>/dev/null; then
        echo "[build] migrated buildinfo.ini -> Scripts/buildinfo.ini" >&2
      fi

    elif [[ -f "${REPO_ROOT}/buildinfo.ini" ]]; then
      # Migrate legacy location -> Scripts/
      mkdirp "${KERNEL_DIR}/Scripts"
      if cp -n "${REPO_ROOT}/buildinfo.ini" "${scripts_ini}" 2>/dev/null; then
        echo "[build] migrated buildinfo.ini -> Scripts/buildinfo.ini" >&2
      fi
    fi

    # If still missing, create a default INI (prefer Scripts/; fallback to gen/).
    if [[ ! -f "${ini}" ]]; then
      mkdirp "$(dirname "${ini}")"
      if ! cat >"${ini}" <<'EOF'
version=0.0.0
build_number=0
build_date=
EOF
      then
        ini="${gen_dir}/buildinfo.ini"
        if [[ ! -f "${ini}" ]]; then
          cat >"${ini}" <<'EOF'
version=0.0.0
build_number=0
build_date=
EOF
        fi
      fi
    fi
  fi

  # Emit buildinfo in a way that never breaks compilation: if the generator
  # fails, a minimal fallback header is produced.
  # NOTE: buildinfo.sh expects the repo root as its first argument so it can
  # compute the git hash.
  emit_buildinfo_files \
    "${REPO_ROOT}" \
    "${ini}" \
    "${gen_include_dir}/buildinfo.h" \
    "${gen_dir}/buildinfo.c"

  # Defensive: if something went sideways, ensure the header exists so
  # freestanding assembly/C can still compile.
  if [[ ! -f "${gen_include_dir}/buildinfo.h" ]]; then
    cat >"${gen_include_dir}/buildinfo.h" <<'EOF'
#pragma once
#define CAPAZ_BUILD_GIT_HASH "unknown"
#define CAPAZ_BUILD_DATE ""
#define CAPAZ_BUILD_VERSION "0.0.0"
EOF
  fi
}

# Emit a device-tree blob header (dtb.h) and a companion C file (dtb.c) into the
# generated directories.
#
# kmain.c expects to `#include "dtb.h"`.
#
# Resolution order for the DTB payload:
#   1) $DTB_INPUT (if set and exists)
#   2) First *.dtb found under common repo locations
#   3) First *.dts found under common repo locations (requires `dtc`)
#
# If no DTB source is found, we emit a *stub* dtb.h/dtb.c so the build can
# proceed, but runtime DTB-dependent features will likely be unavailable.
emit_dtb_artifacts() {
  local gen_inc="$1"
  local gen_dir="$2"

  local dtb_input="${DTB_INPUT:-}"
  local dts_input=""
  local dtb_built=""

  if [ -n "${dtb_input}" ] && [ ! -f "${dtb_input}" ]; then
    echo "warning: DTB_INPUT was set but file does not exist: ${dtb_input}" >&2
    dtb_input=""
  fi

  if [ -z "${dtb_input}" ]; then
    local candidates=(
      "${KERNEL_DIR}/DeviceTree"/*.dtb
      "${KERNEL_DIR}/DeviceTrees"/*.dtb
      "${KERNEL_DIR}/Resources"/*.dtb
      "${KERNEL_DIR}/Sources"/**/*.dtb
    )
    for c in "${candidates[@]}"; do
      if [ -f "${c}" ]; then
        dtb_input="${c}"
        break
      fi
    done
  fi

  if [ -z "${dtb_input}" ]; then
    local candidates_dts=(
      "${KERNEL_DIR}/DeviceTree"/*.dts
      "${KERNEL_DIR}/DeviceTrees"/*.dts
      "${KERNEL_DIR}/Resources"/*.dts
      "${KERNEL_DIR}/Sources"/**/*.dts
    )
    for c in "${candidates_dts[@]}"; do
      if [ -f "${c}" ]; then
        dts_input="${c}"
        break
      fi
    done
  fi

  if [ -n "${dts_input}" ]; then
    if command -v dtc >/dev/null 2>&1; then
      dtb_built="${gen_dir}/capazos.dtb"
      echo "[dtb] compiling DTS -> DTB: ${dts_input} -> ${dtb_built}" >&2
      dtc -I dts -O dtb -o "${dtb_built}" "${dts_input}"
      dtb_input="${dtb_built}"
    else
      echo "warning: found DTS (${dts_input}) but 'dtc' is not installed; emitting stub dtb.h/dtb.c" >&2
      dtb_input=""
    fi
  fi

  local out_h="${gen_inc}/dtb.h"
  local out_c="${gen_dir}/dtb.c"

  if [ -n "${dtb_input}" ] && [ -f "${dtb_input}" ]; then
    echo "[dtb] embedding DTB: ${dtb_input}" >&2
    python3 - "${dtb_input}" "${out_h}" "${out_c}" <<'PY'
import sys
from pathlib import Path

dtb_path = Path(sys.argv[1])
out_h = Path(sys.argv[2])
out_c = Path(sys.argv[3])

data = dtb_path.read_bytes()

hdr = """#ifndef CAPAZOS_DTB_H
#define CAPAZOS_DTB_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const uint8_t g_dtb_blob[];
extern const size_t  g_dtb_blob_len;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // CAPAZOS_DTB_H
"""
out_h.write_text(hdr)

def format_bytes(bs: bytes, cols: int = 12) -> str:
    parts = [f"0x{b:02x}" for b in bs]
    lines = []
    for i in range(0, len(parts), cols):
        lines.append(", ".join(parts[i:i+cols]))
    return ",\n  ".join(lines)

csrc = """#include \"dtb.h\"\n\nconst uint8_t g_dtb_blob[] = {\n  %s\n};\n\nconst size_t g_dtb_blob_len = %d;\n""" % (format_bytes(data), len(data))
out_c.write_text(csrc)
PY
  else
    echo "warning: no DTB found; emitting stub dtb.h/dtb.c (set DTB_INPUT to embed one)" >&2
    cat >"${out_h}" <<'EOF'
#ifndef CAPAZOS_DTB_H
#define CAPAZOS_DTB_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const uint8_t g_dtb_blob[];
extern const size_t  g_dtb_blob_len;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // CAPAZOS_DTB_H
EOF
    cat >"${out_c}" <<'EOF'
#include "dtb.h"

// Stub DTB payload. Provide a real DTB by setting $DTB_INPUT to a *.dtb file
// (or add a *.dtb under Kernel/DeviceTree or Kernel/Resources).
const uint8_t g_dtb_blob[] = { };
const size_t  g_dtb_blob_len = 0;
EOF
  fi
}

# --- Compilation -------------------------------------------------------------

compile_objects() {
  local obj_dir="$1"
  local gen_include_dir="$2"
  shift 2
  local sources=("$@")

  mkdirp "${obj_dir}"

  for src in "${sources[@]}"; do
    local rel
    rel="${src#${KERNEL_DIR}/}"
    rel="${rel//\//_}"
    local obj="${obj_dir}/${rel%.*}.o"

    case "${src}" in
      *.c)
        "${CC}" "${CFLAGS_COMMON_ARR[@]}" \
          -I "${KERNEL_DIR}/Sources" \
          -I "${KERNEL_DIR}/Sources/ABI" \
          -I "${KERNEL_DIR}/Sources/HAL" \
          -I "${KERNEL_DIR}/Sources/Lib" \
          -I "${KERNEL_DIR}/Sources/Lib/fdt" \
          -I "${KERNEL_DIR}/Sources/Arch" \
          -I "${KERNEL_DIR}/Sources/Arch/aarch64" \
          -I "${KERNEL_DIR}/Sources/Arch/aarch64/boot" \
          -I "${KERNEL_DIR}/Sources/Kernel" \
          -I "${KERNEL_DIR}/Sources/Kernel/boot" \
          -I "${KERNEL_DIR}/Sources/Kernel/alloc" \
          -I "${KERNEL_DIR}/Sources/Kernel/cap" \
          -I "${KERNEL_DIR}/Sources/Kernel/core" \
          -I "${KERNEL_DIR}/Sources/Kernel/debug" \
          -I "${KERNEL_DIR}/Sources/Kernel/ipc" \
          -I "${KERNEL_DIR}/Sources/Kernel/irg" \
          -I "${KERNEL_DIR}/Sources/Kernel/irq" \
          -I "${KERNEL_DIR}/Sources/Kernel/mm" \
          -I "${KERNEL_DIR}/Sources/Kernel/platform" \
          -I "${KERNEL_DIR}/Sources/Kernel/sched" \
          -I "${KERNEL_DIR}/Sources/Kernel/task" \
          -I "${KERNEL_DIR}/Sources/Kernel/util" \
          -I "${KERNEL_DIR}/Sources/Kernel/work" \
          -I "${gen_include_dir}" \
          -c "${src}" -o "${obj}"
        ;;
      *.S|*.s)
        "${CC}" "${ASFLAGS_COMMON_ARR[@]}" ${INCLUDE_FLAGS[@]+"${INCLUDE_FLAGS[@]}"} \
          -I "${KERNEL_DIR}/Sources" \
          -I "${KERNEL_DIR}/Sources/ABI" \
          -I "${KERNEL_DIR}/Sources/HAL" \
          -I "${KERNEL_DIR}/Sources/Lib" \
          -I "${KERNEL_DIR}/Sources/Lib/fdt" \
          -I "${KERNEL_DIR}/Sources/Arch" \
          -I "${KERNEL_DIR}/Sources/Arch/aarch64" \
          -I "${KERNEL_DIR}/Sources/Arch/aarch64/boot" \
          -I "${KERNEL_DIR}/Sources/Kernel" \
          -I "${KERNEL_DIR}/Sources/Kernel/boot" \
          -I "${KERNEL_DIR}/Sources/Kernel/alloc" \
          -I "${KERNEL_DIR}/Sources/Kernel/cap" \
          -I "${KERNEL_DIR}/Sources/Kernel/core" \
          -I "${KERNEL_DIR}/Sources/Kernel/debug" \
          -I "${KERNEL_DIR}/Sources/Kernel/ipc" \
          -I "${KERNEL_DIR}/Sources/Kernel/irg" \
          -I "${KERNEL_DIR}/Sources/Kernel/irq" \
          -I "${KERNEL_DIR}/Sources/Kernel/mm" \
          -I "${KERNEL_DIR}/Sources/Kernel/platform" \
          -I "${KERNEL_DIR}/Sources/Kernel/sched" \
          -I "${KERNEL_DIR}/Sources/Kernel/task" \
          -I "${KERNEL_DIR}/Sources/Kernel/util" \
          -I "${KERNEL_DIR}/Sources/Kernel/work" \
          -I "${gen_include_dir}" \
          -c "${src}" -o "${obj}"
        ;;
      *)
        die "unknown source type: ${src}"
        ;;
    esac
  done
}

# --- Link --------------------------------------------------------------------
link_kernel() {
  local out="$1"
  local link_script="$2"
  shift 2
  local objs=("$@")

  need_cmd "${LD}"

  "${LD}" "${LDFLAGS_COMMON_ARR[@]}" ${LINK_EXTRA_FLAGS[@]+"${LINK_EXTRA_FLAGS[@]}"} \
    -T "${link_script}" \
    -o "${out}" \
    "${objs[@]}"
}

# --- Objcopy / image ---------------------------------------------------------

make_binary() {
  local elf="$1"
  local bin="$2"

  need_cmd "${OBJCOPY}"

  "${OBJCOPY}" -O binary "${elf}" "${bin}"
}

# --- Platform config ---------------------------------------------------------

select_platform() {
  # Example defaults; build.sh should set PLATFORM.
  case "${PLATFORM}" in
    aarch64-virt|arm64-virt)
      ARCH="aarch64"
      ;;
    *)
      die "unsupported PLATFORM: ${PLATFORM}"
      ;;
  esac
}

# --- Entrypoint used by build.sh --------------------------------------------

# Build Core artifacts and link them into the kernel image.
#
#     --target core is implemented as:
#   1) Core/Scripts/build_core.sh -> publishes core_c.o + core_swift.o
#   2) Kernel build -> final link consumes those artifacts (Kernel does NOT scan Core sources)
build_core_and_kernel() {
  select_platform
  preflight_common

  local out_dir="${OUT_DIR:?}"
  local core_out="${out_dir}/core"
  mkdirp "${core_out}"

  local core_build="${REPO_ROOT}/Code/Core/Scripts/build_core.sh"
  [[ -f "${core_build}" ]] || preflight_fail "Missing script: Core/Scripts/build_core.sh"

  echo "[build] Building Core artifacts..."
  "${core_build}" \
    --platform "${PLATFORM}" \
    --config "${CONFIG}" \
    --out "${core_out}" \
    --kernel-abi "${KERNEL_DIR}/Sources/ABI"

  local core_c="${core_out}/core_c.o"
  local core_swift="${core_out}/core_swift.o"

  if [[ ! -f "${core_c}" || ! -f "${core_swift}" ]]; then
    preflight_fail "Core build did not produce expected outputs:" \
                   "expected: ${core_c}" \
                   "expected: ${core_swift}"
  fi

  echo "[build] Linking kernel with Core artifacts..."
  build_boot_and_kernel "${core_c}" "${core_swift}"
}

build_boot_and_kernel() {
  # Optional extra objects to link into the kernel ELF (e.g. Core artifacts for --target core).
  # Use an explicit array declaration. With `set -u`, some shells can treat an
  # empty `local x=("$@")` as unset and later fail on `${x[@]}`.
  local -a extra_kern_objs=()
  extra_kern_objs=("$@")

  select_platform
  preflight_common

  need_cmd "${CC}"
  need_cmd "${AR}"
  need_cmd "${LD}"
  need_cmd "${OBJCOPY}"

  local out_dir="${OUT_DIR:?}"
  local boot_obj_dir="${out_dir}/boot/obj"
  local kern_obj_dir="${out_dir}/kernel/obj"
  local gen_dir="${out_dir}/gen"
  local gen_inc="${gen_dir}/include"

  mkdirp "${out_dir}"
  mkdirp "${boot_obj_dir}"
  mkdirp "${kern_obj_dir}"
  mkdirp "${gen_dir}"
  mkdirp "${gen_inc}"

  emit_buildinfo_header "${gen_inc}" "${gen_dir}"
  # Device-tree parsing lives in Sources/Kernel/platform (dtb.h/dtb.c).
  # The kernel receives the DTB pointer via boot_info; we do not generate/embed a DTB here.

  # Collect sources (deterministic order).
  local boot_sources=(
    "${KERNEL_DIR}/Sources/Arch/aarch64/start.S"
  )

  local kernel_sources=()

  # Kernel + HAL sources.
  while IFS= read -r f; do kernel_sources+=("${f}"); done < <(
    list_sources_sorted "${KERNEL_DIR}/Sources/Kernel" "${KERNEL_DIR}/Sources/HAL" -- \
      -type f \( -name "*.c" -o -name "*.S" -o -name "*.s" \)
  )

  # Arch sources (excluding boot start.S).
  while IFS= read -r f; do kernel_sources+=("${f}"); done < <(
    list_sources_sorted "${KERNEL_DIR}/Sources/Arch/aarch64" -- \
      -type f \( -name "*.c" -o -name "*.S" -o -name "*.s" \) ! -name "start.S"
  )

  # Compile generated build metadata as part of the kernel, if present.
  if [ -f "${gen_dir}/buildinfo.c" ]; then
    kernel_sources+=("${gen_dir}/buildinfo.c")
  fi

  compile_objects "${boot_obj_dir}" "${gen_inc}" "${boot_sources[@]}"
  compile_objects "${kern_obj_dir}" "${gen_inc}" "${kernel_sources[@]}"

  # Collect objects (deterministic order).
  local boot_objs=()
  local kern_objs=()

  while IFS= read -r -d '' f; do boot_objs+=("$f"); done < <(list_files_sorted0 "${boot_obj_dir}" "*.o")
  while IFS= read -r -d '' f; do kern_objs+=("$f"); done < <(list_files_sorted0 "${kern_obj_dir}" "*.o")

  local boot_elf="${out_dir}/boot.elf"
  local kern_elf="${out_dir}/kernel.elf"
  local boot_bin="${out_dir}/boot.bin"
  local kern_bin="${out_dir}/kernel.bin"

  link_kernel "${boot_elf}" "${KERNEL_DIR}/Linker/boot.ld" "${boot_objs[@]}"
  # Optional extra kernel objects. With 'set -u', expanding an unset array
  # (common on macOS's /bin/bash 3.2) trips "unbound variable".
  # The ${var+word} form is safe under nounset: it expands to 'word' only if
  # the variable is set.
  link_kernel "${kern_elf}" "${KERNEL_DIR}/Linker/kernel.ld" \
    "${kern_objs[@]}" \
    ${extra_kern_objs[@]+"${extra_kern_objs[@]}"}

  make_binary "${boot_elf}" "${boot_bin}"
  make_binary "${kern_elf}" "${kern_bin}"

  # Combined boot+kernel image. The boot stage expects the kernel to begin at a
  # 2MiB boundary from the start of the image (see Sources/Kernel/boot/kernel_image.h).
  local kern_img="${out_dir}/kernel.img"
  build_kernel_img "${kern_img}" "${boot_bin}" "${kern_bin}"

  echo "[build] Built -> ${boot_bin}, ${kern_bin}, ${kern_img}"
}

# Create kernel.img from boot.bin + padding + kernel.bin.
# Layout:
#   [boot.bin][zero padding to 2MiB boundary][kernel.bin]
build_kernel_img() {
  local out_img="$1"
  local boot_bin="$2"
  local kern_bin="$3"

  need_cmd wc
  need_cmd dd
  need_cmd cat

  local align=$((2 * 1024 * 1024))

  # wc output can include leading spaces; strip them.
  local boot_size
  boot_size="$(wc -c < "${boot_bin}" | tr -d '[:space:]')"

  if ! [[ "${boot_size}" =~ ^[0-9]+$ ]]; then
    die "failed to determine size of ${boot_bin}"
  fi

  local mod=$((boot_size % align))
  local pad=$(( (align - mod) % align ))

  rm -f "${out_img}"
  cat "${boot_bin}" > "${out_img}"

  if [ "${pad}" -ne 0 ]; then
    # Append zero padding (at most 2MiB-1) so the kernel begins at the next 2MiB boundary.
    dd if=/dev/zero bs=1 count="${pad}" status=none >> "${out_img}"
  fi

  cat "${kern_bin}" >> "${out_img}"
}
