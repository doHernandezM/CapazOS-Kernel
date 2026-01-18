#!/usr/bin/env bash
# Capaz build metadata generation.
# Compatible with macOS /bin/bash 3.2.

# shellcheck source=lib/common.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"

buildinfo__read_kv() {
  local file="$1" key="$2"
  [[ -f "$file" ]] || return 0
  awk -v k="$key" '
    function trim(s) { sub(/^[ \t\r\n]+/, "", s); sub(/[ \t\r\n]+$/, "", s); return s }
    /^[ \t\r\n]*$/ { next }
    /^[ \t]*[;#]/ { next }
    /^[ \t]*\[/ { next }
    {
      line=$0
      # strip inline comments (keep it simple: ; or # starts a comment)
      sub(/[ \t]*[;#].*$/, "", line)
      n = split(line, parts, "=")
      if (n < 2) next
      kk = trim(parts[1])
      if (kk != k) next
      vv = substr(line, index(line, "=") + 1)
      vv = trim(vv)
      val = vv
    }
    END { if (val != "") print val }
  ' "$file" || true
}

buildinfo__resolve_field() {
  # Precedence: explicit env var > existing ini value > default.
  local env_var="$1" ini_path="$2" ini_key="$3" default_value="$4"
  local v="${!env_var:-}"
  if [[ -z "$v" ]]; then
    v="$(buildinfo__read_kv "$ini_path" "$ini_key")"
  fi
  if [[ -z "$v" ]]; then
    v="$default_value"
  fi
  printf '%s' "$v"
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

  # Resolve other fields.
  # Precedence is env > ini > defaults so Xcode can override, but user-editing
  # buildinfo.ini also works as expected.
  local boot_version boot_platform kernel_version machine kernel_name core_version core_name
  boot_version="$(buildinfo__resolve_field BOOT_VERSION "$ini_path" boot_version "0.0.0")"
  boot_platform="$(buildinfo__resolve_field BOOT_PLATFORM "$ini_path" boot_platform "AArch64")"
  kernel_version="$(buildinfo__resolve_field KERNEL_VERSION "$ini_path" kernel_version "0.0.0")"
  machine="$(buildinfo__resolve_field MACHINE "$ini_path" machine "Virt")"
  kernel_name="$(buildinfo__resolve_field KERNEL_NAME "$ini_path" kernel_name "Capaz Kernel")"
  core_version="$(buildinfo__resolve_field CORE_VERSION "$ini_path" core_version "0.0.0")"
  core_name="$(buildinfo__resolve_field CORE_NAME "$ini_path" core_name "Capaz Core")"

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
