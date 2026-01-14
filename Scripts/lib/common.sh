#!/usr/bin/env bash
set -euo pipefail

# Resolve script directory (works when sourced).
_common_this_file="${BASH_SOURCE[0]}"
COMMON_DIR="$(cd "$(dirname "${_common_this_file}")" && pwd)"
SCRIPTS_DIR="$(cd "${COMMON_DIR}/.." && pwd)"
ROOT_DIR="$(cd "${SCRIPTS_DIR}/.." && pwd)"

log() { printf '%s\n' "$*" >&2; }
die() { log "error: $*"; exit 1; }

# Cross-platform file size in bytes.
file_size() {
  local f="$1"
  [[ -f "$f" ]] || die "file not found: $f"
  if stat -c%s "$f" >/dev/null 2>&1; then
    stat -c%s "$f"
  else
    stat -f%z "$f"
  fi
}

# align_up(value, alignment)
align_up() {
  local v="$1" a="$2"
  echo $(( (v + a - 1) / a * a ))
}

# pad_file_to(file, alignment) -> echoes pad bytes
pad_file_to() {
  local f="$1" a="$2"
  local sz; sz="$(file_size "$f")"
  local padded; padded="$(align_up "$sz" "$a")"
  echo $(( padded - sz ))
}

# ensure_dir(path)
ensure_dir() { mkdir -p "$1"; }

# join_paths base rel
join_paths() {
  local base="$1" rel="$2"
  (cd "$base" && cd "$rel" && pwd)
}

# read_ini_value(file, section, key)
# - Supports traditional INI with [section] headers and key = value pairs.
# - Also supports flat key=value files (section ignored) for backwards compatibility.
read_ini_value() {
  local file="$1" section="$2" key="$3"
  [[ -f "$file" ]] || { echo ""; return 0; }

  # Fast path: flat key=value
  local v
  v=$(grep -E "^${key}=" "$file" | tail -n 1 | cut -d= -f2- 2>/dev/null || true)
  if [[ -n "$v" ]]; then
    echo "$v"
    return 0
  fi

  # INI parsing: look inside the requested section.
  # Accept: key = value  (spaces optional)
  awk -v section="$section" -v key="$key" '
    BEGIN{in=0}
    /^\[[^\]]+\]/{
      gsub(/\[|\]/, "", $0)
      in=($0==section)
      next
    }
    in==1 {
      # strip leading/trailing spaces
      line=$0
      sub(/^[[:space:]]+/, "", line)
      if (line ~ "^" key "[[:space:]]*=") {
        sub("^" key "[[:space:]]*=[[:space:]]*", "", line)
        sub(/[[:space:]]+$/, "", line)
        print line
        exit
      }
    }
  ' "$file" 2>/dev/null || true
}
