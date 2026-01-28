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

# Support bumping arbitrary numeric keys in buildinfo.ini.  The default key
# remains 'build_number' for backwards compatibility.  You can invoke this
# script as:
#   bump_build_number.sh [--key <ini_key>] [<ini_path>]
# If --key is provided, the following argument is taken as the key to bump.
# Any remaining positional argument is interpreted as the ini file path.  If
# omitted, the script falls back to Kernel/Scripts/buildinfo.ini and other
# legacy locations as before.

key_name="build_number"
ini_arg=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --key)
      if [[ $# -lt 2 ]]; then
        echo "error: --key requires an argument" >&2
        exit 1
      fi
      key_name="$2"
      shift 2
      ;;
    *)
      # First non-flag argument is the ini path.  Preserve for later.
      if [[ -z "${ini_arg}" ]]; then
        ini_arg="$1"
      else
        echo "error: unexpected argument: $1" >&2
        exit 1
      fi
      shift
      ;;
  esac
done

INI="${ini_arg}" 
if [[ -z "${INI}" ]]; then
  INI="${KERNEL_DIR}/Scripts/buildinfo.ini"
fi

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

current="$(read_key "${key_name}")"
[[ -n "${current}" ]] || current="0"

if ! [[ "${current}" =~ ^[0-9]+$ ]]; then
  echo "error: ${key_name} is not numeric in ${INI}: '${current}'" >&2
  exit 1
fi

next=$((current + 1))
today_utc="$(date -u +%Y-%m-%d)"

# Rewrite atomically.
cp "${INI}" "${tmp}"

# Update the numeric key.
if grep -qE "^[[:space:]]*${key_name}[[:space:]]*=" "${tmp}"; then
  perl -0777 -i -pe "s/^[[:space:]]*${key_name}[[:space:]]*=.*$/${key_name} = ${next}/m" "${tmp}"
else
  printf '\n%s = %s\n' "${key_name}" "${next}" >> "${tmp}"
fi

# Always update build_date when bumping any build number.
if grep -qE '^[[:space:]]*build_date[[:space:]]*=' "${tmp}"; then
  perl -0777 -i -pe "s/^[[:space:]]*build_date[[:space:]]*=.*$/build_date = ${today_utc}/m" "${tmp}"
else
  printf 'build_date = %s\n' "${today_utc}" >> "${tmp}"
fi

mv "${tmp}" "${INI}"
echo "${next}"
