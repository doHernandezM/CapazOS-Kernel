#!/bin/bash

set -euo pipefail

# Archive sources and build/kernel.img into <repo>/archive/OS.<kernel_build_number>.zip
#
# Usage:
#   archive.sh [REPO_ROOT] [KERNEL_IMG_PATH] [BUILDINFO_INI]
#
# If args are omitted, defaults are derived from this script's location.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OS_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"  # .../(Code/)OS

# Supported layouts:
#   A) <repo>/Code/OS   (legacy workspace layout)
#   B) <repo>/OS        (OS is the repo root)

DEFAULT_REPO_ROOT=""
if [[ "$(basename "${OS_DIR}")" == "OS" && "$(basename "$(dirname "${OS_DIR}")")" == "Code" ]]; then
  DEFAULT_REPO_ROOT="$(cd "${OS_DIR}/../.." && pwd)"
else
  DEFAULT_REPO_ROOT="${OS_DIR}"
fi

REPO_ROOT="${1:-${DEFAULT_REPO_ROOT}}"
KERNEL_IMG_PATH="${2:-${REPO_ROOT}/build/kernel.img}"
BUILDINFO_INI="${3:-${OS_DIR}/Scripts/buildinfo.ini}"

ARCHIVE_DIR="${REPO_ROOT}/archive"

ini_get() {
  local key="$1"
  local file="$2"
  # Match "key = value" or "key=value"; trim whitespace.
  awk -F= -v k="$key" '
    $1 ~ /^[[:space:]]*[^#;].*/ {
      gsub(/^[[:space:]]+|[[:space:]]+$/, "", $1)
      if ($1 == k) {
        $1=""; sub(/^=/, "")
        gsub(/^[[:space:]]+|[[:space:]]+$/, "", $0)
        print $0
        exit
      }
    }
  ' "$file"
}

if [[ ! -f "${BUILDINFO_INI}" ]]; then
  echo "[archive] error: missing buildinfo.ini at ${BUILDINFO_INI}" >&2
  exit 1
fi

BUILD_NUMBER="$(ini_get kernel_build_number "${BUILDINFO_INI}" || true)"
if [[ -z "${BUILD_NUMBER}" ]]; then
  # Fall back to legacy key for backwards compatibility
  BUILD_NUMBER="$(ini_get build_number "${BUILDINFO_INI}" || true)"
fi

if [[ -z "${BUILD_NUMBER}" ]]; then
  echo "[archive] error: kernel_build_number missing in ${BUILDINFO_INI}" >&2
  exit 1
fi

if [[ ! -f "${KERNEL_IMG_PATH}" ]]; then
  echo "[archive] error: kernel.img missing at ${KERNEL_IMG_PATH}" >&2
  exit 1
fi

mkdir -p "${ARCHIVE_DIR}"

OUT_ZIP="${ARCHIVE_DIR}/OS.${BUILD_NUMBER}.zip"
rm -f "${OUT_ZIP}"

pushd "${REPO_ROOT}" >/dev/null
  # First, archive sources (exclude DerivedData-like outputs). Then add kernel.img.
  if [[ -d "Code" ]]; then
    zip -qry "${OUT_ZIP}" "Code" -x "__MACOSX/*" 
  elif [[ -d "OS" ]]; then
    zip -qry "${OUT_ZIP}" "OS" -x "__MACOSX/*" 
  else
    # Repo root is the OS directory itself (or an unknown layout). Archive the
    # working tree but exclude build products and prior archives.
    zip -qry "${OUT_ZIP}" "." -x "__MACOSX/*" -x "build/*" -x "archive/*"
  fi

  # Ensure kernel image is included even if build/ was excluded above.
  zip -q "${OUT_ZIP}" "build/kernel.img"
popd >/dev/null

echo "[archive] Wrote -> ${OUT_ZIP}"
