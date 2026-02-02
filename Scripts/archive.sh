#!/bin/sh
set -eu

REPO_ROOT="${1:?repo root required}"
BUILD_DIR="${2:?build dir required}"
BUILDINFO_INI="${3:?buildinfo.ini required}"

log() { printf "%s\n" "$*"; }

# Read build number from ini (works with your existing buildinfo format)
BUILD_NUMBER="$(
python3 - <<'PY' "$BUILDINFO_INI"
import configparser, sys
p = configparser.ConfigParser()
p.read(sys.argv[1])
print(p.get("build", "number"))
PY
)"

ARCHIVE_DIR="${REPO_ROOT}/archive"
CODE_DIR="${REPO_ROOT}/Code"
CODE_OS_DIR="${CODE_DIR}/OS"

mkdir -p "${ARCHIVE_DIR}"

SRC_ZIP_NAME="OS.${BUILD_NUMBER}.zip"
SRC_ZIP_ARCHIVE="${ARCHIVE_DIR}/${SRC_ZIP_NAME}"
SRC_ZIP_CODE="${CODE_DIR}/${SRC_ZIP_NAME}"

KERNEL_IMG="${BUILD_DIR}/kernel.img"
KERNEL_ZIP_NAME="Kernel.${BUILD_NUMBER}.zip"
KERNEL_ZIP="${ARCHIVE_DIR}/${KERNEL_ZIP_NAME}"

# Move previous build's source zip to Trash (from both archive/ and Code/)
# If Trash doesn't exist (non-mac), delete instead.
trash_file() {
    f="$1"
    [ -e "$f" ] || return 0
    if [ -d "${HOME}/.Trash" ]; then
        log "Move to Trash: $f"
        mv -f "$f" "${HOME}/.Trash/"
    else
        log "Remove old archive: $f"
        rm -f "$f"
    fi
}

# compute previous build number if numeric
case "$BUILD_NUMBER" in
    ''|*[!0-9]*)
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
fi

# A) Zip Code/OS/ into archive/ and Code/ as OS.<build>.zip
log "Create source archive: ${SRC_ZIP_ARCHIVE} (from Code/OS/)"
rm -f "${SRC_ZIP_ARCHIVE}" "${SRC_ZIP_CODE}"

# zip the OS folder (so the zip contains OS/...)
(
    cd "${CODE_DIR}"
    zip -qry "${SRC_ZIP_ARCHIVE}" "OS"
)

# copy the same zip into Code/
cp -f "${SRC_ZIP_ARCHIVE}" "${SRC_ZIP_CODE}"

# C) Zip kernel.img on its own into archive/
if [ ! -f "${KERNEL_IMG}" ]; then
    log "WARNING: missing ${KERNEL_IMG}; skipping kernel zip"
else
    log "Create kernel archive: ${KERNEL_ZIP}"
    rm -f "${KERNEL_ZIP}"
    # -j to avoid storing full path; zip contains just kernel.img
    zip -jqry "${KERNEL_ZIP}" "${KERNEL_IMG}"
fi

log "Archive complete."
