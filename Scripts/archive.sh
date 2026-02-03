#!/bin/sh
set -eu

REPO_ROOT="${1:?repo root required}"
BUILD_DIR="${2:?build dir required}"
BUILDINFO_INI="${3:?buildinfo.ini required}"

log() { printf "%s\n" "$*"; }

# Read build number from buildinfo.ini.
#
# Supported formats across builds:
#   [kernel] kernel_build_number=NNN   (current)
#   [build]  build_number=NNN         (legacy)
#   [build]  number=NNN               (legacy)
BUILD_NUMBER="$(
awk '
function trim(s) {
    gsub(/^[[:space:]]+/, "", s)
    gsub(/[[:space:]]+$/, "", s)
    return s
}
BEGIN {
    sec = ""
    best = ""
    bestw = 999999
}
function consider(w, v) {
    if (v == "") return
    if (w < bestw) { bestw = w; best = v }
}
{
    line = $0
    gsub(/\r/, "", line)

    # Section header
    if (line ~ /^[[:space:]]*\[/) {
        sec = tolower(trim(line))
        next
    }
    # Comment or blank
    if (line ~ /^[[:space:]]*([#;]|$)/) next

    # key=value line
    eq = index(line, "=")
    if (eq == 0) next
    key = tolower(trim(substr(line, 1, eq-1)))
    val = trim(substr(line, eq+1))
    sub(/[[:space:]]*[#;].*$/, "", val)
    val = trim(val)
    if (val !~ /^[0-9]+$/) next

    if (sec == "[kernel]") {
        if (key == "kernel_build_number") consider(1, val)
        else if (key == "build_number") consider(2, val)
    } else if (sec == "[build]") {
        if (key == "kernel_build_number") consider(3, val)
        else if (key == "build_number") consider(4, val)
        else if (key == "number") consider(7, val)
    } else if (sec == "") {
        if (key == "kernel_build_number") consider(5, val)
        else if (key == "build_number") consider(6, val)
        else if (key == "number") consider(8, val)
    }
}
END {
    if (best != "") { print best; exit 0 }
    exit 1
}
' "$BUILDINFO_INI" 2>/dev/null || true
)"
if [ -z "${BUILD_NUMBER:-}" ]; then
    log "ERROR: Could not read build number from ${BUILDINFO_INI}"
    exit 1
fi

ARCHIVE_DIR="${REPO_ROOT}/archive"
CODE_DIR="${REPO_ROOT}/Code"
CODE_OS_DIR="${CODE_DIR}/OS"

mkdir -p "${ARCHIVE_DIR}"

SRC_ZIP_NAME="OS.${BUILD_NUMBER}.zip"
SRC_ZIP_ARCHIVE="${ARCHIVE_DIR}/${SRC_ZIP_NAME}"
SRC_ZIP_CODE="${CODE_DIR}/${SRC_ZIP_NAME}"
SRC_ZIP_CODE_OS="${CODE_OS_DIR}/${SRC_ZIP_NAME}"

KERNEL_IMG="${BUILD_DIR}/kernel.img"
KERNEL_ZIP_NAME="Kernel.${BUILD_NUMBER}.zip"
KERNEL_ZIP="${ARCHIVE_DIR}/${KERNEL_ZIP_NAME}"

cleanup_on_error() {
    rm -f "${SRC_ZIP_ARCHIVE}" "${SRC_ZIP_CODE}" "${SRC_ZIP_CODE_OS}" "${KERNEL_ZIP}"
}
trap cleanup_on_error ERR

# Move previous build's source zip to Trash (from archive/, Code/, and Code/OS/)
# If Trash doesn't exist (non-mac), delete instead.
trash_file() {
    f="$1"
    [ -e "$f" ] || return 0
    if [ -d "${HOME}/.Trash" ]; then
        log "Move to Trash: $f"
        if ! mv -f "$f" "${HOME}/.Trash/"; then
            log "Trash move failed; removing: $f"
            rm -f "$f"
        fi
    else
        log "Remove old archive: $f"
        rm -f "$f"
    fi
}

# compute previous build number if numeric
case "$BUILD_NUMBER" in
    ""|*[!0-9]*)
        PREV_BUILD=""
        ;;
    *)
        if [ "$BUILD_NUMBER" -gt 0 ]; then
            PREV_BUILD=$((BUILD_NUMBER - 1))
        else
            PREV_BUILD=""
        fi
        ;;
esac

if [ -n "${PREV_BUILD:-}" ]; then
    trash_file "${ARCHIVE_DIR}/OS.${PREV_BUILD}.zip"
    trash_file "${CODE_DIR}/OS.${PREV_BUILD}.zip"
    trash_file "${CODE_OS_DIR}/OS.${PREV_BUILD}.zip"
fi

# A) Zip Code/OS/ into archive/ and copy into both Code/ and Code/OS/
log "Create source archive: ${SRC_ZIP_ARCHIVE} (from Code/OS/)"
rm -f "${SRC_ZIP_ARCHIVE}" "${SRC_ZIP_CODE}" "${SRC_ZIP_CODE_OS}"

# zip the OS folder (so the zip contains OS/...)
(
    cd "${CODE_DIR}"
    zip -qry "${SRC_ZIP_ARCHIVE}" "OS"
)

# C) Zip kernel.img on its own into archive/
if [ ! -f "${KERNEL_IMG}" ]; then
    log "WARNING: missing ${KERNEL_IMG}; skipping kernel zip"
else
    log "Create kernel archive: ${KERNEL_ZIP}"
    rm -f "${KERNEL_ZIP}"
    # -j to avoid storing full path; zip contains just kernel.img
    zip -jqry "${KERNEL_ZIP}" "${KERNEL_IMG}"
fi

# copy the same zip into Code/ and Code/OS/ after all archive steps succeed
cp -f "${SRC_ZIP_ARCHIVE}" "${SRC_ZIP_CODE}"
cp -f "${SRC_ZIP_ARCHIVE}" "${SRC_ZIP_CODE_OS}"

log "Archive complete."
