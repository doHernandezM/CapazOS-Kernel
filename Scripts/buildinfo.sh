#!/usr/bin/env bash

# Simplified buildinfo flow.
#
# - Single source of truth: buildinfo.ini
# - Everything read from buildinfo.ini is treated as a string except
#   build_number, which is treated as an integer.
# - This script does not try to enforce policy decisions (no version
#   splitting, no aliases, etc.). It simply maps ini keys to C/asm-visible
#   macros.
# - Generates:
#     - buildinfo.h   (start.S expects this)
#     //////////////- build_info.h  (kmain.c expects this)
#     - buildinfo.c   (optional; currently unused by the kernel sources)
#
# This file is meant to be sourced by other scripts.

buildinfo__log() {
  printf "[buildinfo] %s\n" "$*" >&2
}

buildinfo__die() {
  printf "[buildinfo] error: %s\n" "$*" >&2
  return 1
}

buildinfo__write_if_changed() {
  local path="$1"
  local tmp
  tmp="$(mktemp)"
  cat >"${tmp}"

  if [[ -f "${path}" ]] && cmp -s "${tmp}" "${path}"; then
    rm -f "${tmp}"
    return 0
  fi

  mkdir -p "$(dirname "${path}")"
  mv "${tmp}" "${path}"
}

buildinfo__read_ini() {
  local ini="$1"
  [[ -f "${ini}" ]] || buildinfo__die "missing ini: ${ini}"

  # Trim helper (portable; no extglob).
  buildinfo__trim() {
    local s="$1"
    # remove leading whitespace
    s="${s#"${s%%[![:space:]]*}"}"
    # remove trailing whitespace
    s="${s%"${s##*[![:space:]]}"}"
    printf '%s' "${s}"
  }

  # Clear only the vars we manage.  When adding new ini keys, append them here.
  # These variables are populated by buildinfo__read_ini and referenced later in
  # emit_buildinfo_files. If you add keys to buildinfo.ini you must also add
  # them here so they don't leak previous values across calls.
  unset build_number version boot_platform machine kernel_name kernel_version boot_version core_name core_version build_date
  # New keys introduced by M7: core_build_number, build_version, build_environment
  unset core_build_number build_version build_environment

  # shellcheck disable=SC2162
  while IFS= read -r line || [[ -n "$line" ]]; do
    # Trim whitespace.
    line="${line%%$'\r'}"
    [[ -z "${line}" ]] && continue
    [[ "${line}" =~ ^[[:space:]]*# ]] && continue
    [[ "${line}" =~ ^[[:space:]]*\; ]] && continue
    [[ "${line}" =~ ^[[:space:]]*\[.*\][[:space:]]*$ ]] && continue

    # key=value
    if [[ "${line}" != *"="* ]]; then
      continue
    fi

    local key="${line%%=*}"
    local val="${line#*=}"
    key="$(buildinfo__trim "${key}")"
    val="$(buildinfo__trim "${val}")"

    # Only allow safe shell identifiers.
    if [[ ! "${key}" =~ ^[A-Za-z_][A-Za-z0-9_]*$ ]]; then
      continue
    fi

    printf -v "${key}" '%s' "${val}"
  done <"${ini}"
}

read_buildinfo_value() {
  local ini="$1" key="$2"
  buildinfo__read_ini "${ini}" || return 1
  printf '%s' "${!key-}"
}

set_buildinfo_value() {
  local ini="$1" key="$2" val="$3"
  [[ -f "${ini}" ]] || buildinfo__die "missing ini: ${ini}"

  # Replace existing key or append.
  if grep -qE "^[[:space:]]*${key}[[:space:]]*=" "${ini}"; then
    # BSD sed compatible.
    sed -i '' -E "s|^[[:space:]]*${key}[[:space:]]*=.*$|${key}=${val}|" "${ini}"
  else
    printf '\n%s=%s\n' "${key}" "${val}" >>"${ini}"
  fi
}

init_buildinfo_ini() {
  local ini="$1"
  [[ -f "${ini}" ]] || touch "${ini}"

  # Only set defaults if missing.
  local cur

  cur="$(read_buildinfo_value "${ini}" build_number || true)"
  [[ -n "${cur}" ]] || set_buildinfo_value "${ini}" build_number "0"

  cur="$(read_buildinfo_value "${ini}" kernel_version || true)"
  [[ -n "${cur}" ]] || set_buildinfo_value "${ini}" kernel_version "0.0.0"

  cur="$(read_buildinfo_value "${ini}" boot_version || true)"
  [[ -n "${cur}" ]] || set_buildinfo_value "${ini}" boot_version "0.0.0"

  cur="$(read_buildinfo_value "${ini}" boot_platform || true)"
  [[ -n "${cur}" ]] || set_buildinfo_value "${ini}" boot_platform "unknown"

  cur="$(read_buildinfo_value "${ini}" machine || true)"
  [[ -n "${cur}" ]] || set_buildinfo_value "${ini}" machine "unknown"

  cur="$(read_buildinfo_value "${ini}" build_date || true)"
  [[ -n "${cur}" ]] || set_buildinfo_value "${ini}" build_date "$(date -u +%Y-%m-%d)"

  # New defaults introduced by M7.  core_build_number is distinct from the
  # kernel build_number and should be zero-initialized.  build_version and
  # build_environment provide a human-readable version string and a build
  # environment identifier.  If these keys already exist in the ini file
  # (perhaps set manually by a developer or CI), we leave them untouched.
  cur="$(read_buildinfo_value "${ini}" core_build_number || true)"
  [[ -n "${cur}" ]] || set_buildinfo_value "${ini}" core_build_number "0"

  cur="$(read_buildinfo_value "${ini}" build_version || true)"
  [[ -n "${cur}" ]] || set_buildinfo_value "${ini}" build_version "0.0.0"

  cur="$(read_buildinfo_value "${ini}" build_environment || true)"
  [[ -n "${cur}" ]] || set_buildinfo_value "${ini}" build_environment "macOS Xcode"
}

buildinfo__git_hash() {
  local repo_root="$1"
  if command -v git >/dev/null 2>&1 && [[ -d "${repo_root}/.git" ]]; then
    (cd "${repo_root}" && git rev-parse --short HEAD 2>/dev/null) || true
  fi
}

emit_buildinfo_files() {
  local repo_root="$1"
  local ini="$2"
  local out_h="$3"
  local out_c="$4"

  buildinfo__read_ini "${ini}" || return 1

  [[ -n "${build_number-}" ]] || buildinfo__die "build_number missing in ${ini}"
  [[ -n "${kernel_version-}" ]] || buildinfo__die "kernel_version missing in ${ini}"
  [[ -n "${boot_platform-}" ]] || buildinfo__die "boot_platform missing in ${ini}"
  [[ -n "${boot_version-}" ]] || buildinfo__die "boot_version missing in ${ini}"
  [[ -n "${machine-}" ]] || buildinfo__die "machine missing in ${ini}"
  [[ -n "${build_date-}" ]] || build_date="$(date -u +%Y-%m-%d)"

  # Before emitting the header and C file we need to determine the version and
  # environment fields.  build_version is the canonical version moving forward,
  # but older ini files may still use "version".  If build_version is empty and
  # version is present, fall back to version.  This preserves backwards
  # compatibility until all callers migrate to build_version.
  local _build_version="${build_version-}"
  if [[ -z "${_build_version}" && -n "${version-}" ]]; then
    _build_version="${version-}"
  fi
  # Provide a sane default if nothing is set.
  [[ -n "${_build_version}" ]] || _build_version="0.0.0"

  # Use the provided build_environment or default to "macOS Xcode" if not set.
  local _build_env="${build_environment-}"
  [[ -n "${_build_env}" ]] || _build_env="macOS Xcode"

  # core_build_number may be empty if not defined in the ini; default to 0.
  local _core_build_number="${core_build_number-}"
  [[ -n "${_core_build_number}" ]] || _core_build_number="0"

  buildinfo__write_if_changed "${out_h}" <<EOF
#pragma once

// Generated from buildinfo.ini. Do not edit.

// NOTE: start.S includes "buildinfo.h" and expects CAPAZ_BOOT_* macros.
//       kmain.c includes "build_info.h" and expects CAPAZ_* macros below.

#define CAPAZ_BUILD_NUMBER ${build_number}
#define CAPAZ_BUILD_DATE "${build_date}"

#define CAPAZ_MACHINE "${machine}"

#define CAPAZ_KERNEL_VERSION "${kernel_version}"
#define CAPAZ_BOOT_PLATFORM "${boot_platform}"
#define CAPAZ_BOOT_VERSION "${boot_version}"

// M7 additional build metadata.  These macros expose the core build
// number, overall build version and the build environment.  They are
// derived from the ini file or fall back to sensible defaults when
// missing.  See init_buildinfo_ini() for details.
#define CAPAZ_CORE_BUILD_NUMBER ${_core_build_number}
#define CAPAZ_BUILD_VERSION "${_build_version}"
#define CAPAZ_BUILD_ENVIRONMENT "${_build_env}"
EOF

  # kmain.c uses build_info.h (underscore). Keep it as a thin wrapper so
  # upstream code doesn't need to change.
  local out_dir
  out_dir="$(dirname "${out_h}")"
  buildinfo__write_if_changed "${out_dir}/build_info.h" <<'EOF'
#pragma once
#include "buildinfo.h"
EOF

  buildinfo__write_if_changed "${out_c}" <<EOF
// Generated from buildinfo.ini. Do not edit.

#include "buildinfo.h"

__attribute__((used)) const unsigned long capaz_build_number = CAPAZ_BUILD_NUMBER;
__attribute__((used)) const char capaz_build_date[] = CAPAZ_BUILD_DATE;
__attribute__((used)) const char capaz_machine[] = CAPAZ_MACHINE;
__attribute__((used)) const char capaz_kernel_version[] = CAPAZ_KERNEL_VERSION;
__attribute__((used)) const char capaz_boot_platform[] = CAPAZ_BOOT_PLATFORM;
__attribute__((used)) const char capaz_boot_version[] = CAPAZ_BOOT_VERSION;
__attribute__((used)) const unsigned long capaz_core_build_number = CAPAZ_CORE_BUILD_NUMBER;
__attribute__((used)) const char capaz_build_version[] = CAPAZ_BUILD_VERSION;
__attribute__((used)) const char capaz_build_environment[] = CAPAZ_BUILD_ENVIRONMENT;
EOF
}
