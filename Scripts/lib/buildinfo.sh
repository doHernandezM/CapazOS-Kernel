#!/usr/bin/env bash
# Capaz build metadata generation.
# Compatible with macOS /bin/bash 3.2.

# shellcheck source=lib/common.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"

buildinfo__read_kv() {
  local file="$1" key="$2"
  [[ -f "$file" ]] || return 0
  grep -E "^${key}=" "$file" | tail -n 1 | cut -d= -f2- || true
}

buildinfo__load_env() {
  # Load build metadata from Scripts/buildinfo.env (if present).
  # Values are exported into the current shell.
  local lib_dir scripts_dir env_path
  lib_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
  scripts_dir="$(cd "${lib_dir}/.." && pwd)"
  env_path="${scripts_dir}/buildinfo.env"

  if [[ -f "${env_path}" ]]; then
    # shellcheck disable=SC1090
    set -a
    source "${env_path}"
    set +a
  fi
}

buildinfo_update_and_generate_header() {
  local ini_path="$1"
  local header_path="$2"

  # Guardrails: both arguments must be files, not directories.
  if [[ -z "${ini_path}" || -d "${ini_path}" ]]; then
    die "buildinfo: ini_path must be a file path (got: ${ini_path})"
  fi
  if [[ -z "${header_path}" || -d "${header_path}" ]]; then
    die "buildinfo: header_path must be a file path (got: ${header_path})"
  fi

  ensure_dir "$(dirname "$ini_path")"
  ensure_dir "$(dirname "$header_path")"

  # Pull in scripted build metadata first.
  buildinfo__load_env

  # ISO-8601 UTC build timestamp (stable + parseable)
  local build_date
  build_date="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"

  # Resolve build_number:
  #  1) Prefer BUILD_NUMBER from Scripts/buildinfo.env (requested behavior)
  #  2) Fall back to previous ini + 1 (legacy behavior)
  local build_number
  if [[ -n "${BUILD_NUMBER:-}" && "${BUILD_NUMBER}" =~ ^[0-9]+$ ]]; then
    build_number="${BUILD_NUMBER}"
  else
    build_number="$(buildinfo__read_kv "$ini_path" "build_number")"
    if [[ -z "${build_number}" || ! "${build_number}" =~ ^[0-9]+$ ]]; then
      build_number=0
    fi
    build_number=$((build_number + 1))
  fi

  # Resolve other fields (prefer env; safe defaults as fallback).
  local boot_version boot_platform kernel_version machine kernel_name core_version core_name
  boot_version="${BOOT_VERSION:-0.0.0}"
  boot_platform="${BOOT_PLATFORM:-AArch64}"
  kernel_version="${KERNEL_VERSION:-0.0.0}"
  machine="${MACHINE:-Virt}"
  kernel_name="${KERNEL_NAME:-Capaz Kernel}"
  core_version="${CORE_VERSION:-0.0.0}"
  core_name="${CORE_NAME:-Capaz Core}"

  cat >"$ini_path" <<EOF2
# Capaz build info (generated)
# This file is overwritten every build.
boot_version=${boot_version}
boot_platform=${boot_platform}
kernel_version=${kernel_version}
machine=${machine}
kernel_name=${kernel_name}
core_version=${core_version}
core_name=${core_name}
build_date=${build_date}
build_number=${build_number}
EOF2

  cat >"$header_path" <<EOF2
#pragma once

#define CAPAZ_BOOT_VERSION "${boot_version}"
#define CAPAZ_BOOT_PLATFORM "${boot_platform}"
#define CAPAZ_KERNEL_VERSION "${kernel_version}"
#define CAPAZ_MACHINE "${machine}"
#define CAPAZ_KERNEL_NAME "${kernel_name}"
#define CAPAZ_CORE_VERSION "${core_version}"
#define CAPAZ_CORE_NAME "${core_name}"

#define CAPAZ_BUILD_DATE "${build_date}"
#define CAPAZ_BUILD_NUMBER ${build_number}
EOF2
}

# Backwards-compatible name used by some build scripts.
generate_build_info() {
  buildinfo_update_and_generate_header "$@"
}
