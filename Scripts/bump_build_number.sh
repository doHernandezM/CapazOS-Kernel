#!/usr/bin/env bash
# Standalone build-number bump tool.
#
# Why this exists:
# - We do NOT auto-increment during every Xcode build (keeps builds reproducible
#   and avoids constantly dirty working trees).
# - Developers/CI can bump or override build numbers explicitly.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KERNEL_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
REPO_ROOT="$(cd "${KERNEL_DIR}/.." && pwd)"

# Determine which key to bump.  Default to kernel_build_number.  Accept optional
# --key <name> in front of the INI path.
KEY_NAME="kernel_build_number"
if [[ "${1:-}" == "--key" ]]; then
  KEY_NAME="${2}"
  shift 2
fi

INI="${1:-${KERNEL_DIR}/Scripts/buildinfo.ini}"
if [[ ! -f "${INI}" ]]; then
  # Backwards-compatible locations
  if [[ -f "${KERNEL_DIR}/buildinfo.ini" ]]; then
    INI="${KERNEL_DIR}/buildinfo.ini"
  elif [[ -f "${REPO_ROOT}/buildinfo.ini" ]]; then
    INI="${REPO_ROOT}/buildinfo.ini"
  else
    echo "error: buildinfo.ini not found (expected ${KERNEL_DIR}/Scripts/buildinfo.ini)" >&2
    exit 1
  fi
fi

tmp="${INI}.tmp.$$"

# Canonical formatting used by the project:
#   key = value

read_key() {
  local key="$1"
  # shellcheck disable=SC2002
  cat "${INI}" \
    | grep -E "^[[:space:]]*${key}[[:space:]]*=" \
    | head -n1 \
    | sed -E "s/^[[:space:]]*${key}[[:space:]]*=//" \
    | sed -E 's/^[[:space:]]+//; s/[[:space:]]+$//'
}

current="$(read_key "${KEY_NAME}")"
[[ -n "${current}" ]] || current="0"

if ! [[ "${current}" =~ ^[0-9]+$ ]]; then
  echo "error: ${KEY_NAME} is not numeric in ${INI}: '${current}'" >&2
  exit 1
fi

next=$((current + 1))
today_utc="$(date -u +%Y-%m-%d)"

# Rewrite atomically.
cp "${INI}" "${tmp}"

if grep -qE "^[[:space:]]*${KEY_NAME}[[:space:]]*=" "${tmp}"; then
  perl -0777 -i -pe "s/^[[:space:]]*${KEY_NAME}[[:space:]]*=.*$/${KEY_NAME} = ${next}/m" "${tmp}"
else
  printf '\n%s = %s\n' "${KEY_NAME}" "${next}" >> "${tmp}"
fi

if grep -qE '^[[:space:]]*build_date[[:space:]]*=' "${tmp}"; then
  perl -0777 -i -pe "s/^[[:space:]]*build_date[[:space:]]*=.*$/build_date = ${today_utc}/m" "${tmp}"
else
  printf 'build_date = %s\n' "${today_utc}" >> "${tmp}"
fi

mv "${tmp}" "${INI}"
echo "${next}"
