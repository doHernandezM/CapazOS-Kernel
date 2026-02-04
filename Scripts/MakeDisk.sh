#!/usr/bin/env bash
set -euo pipefail

usage() {
  echo "Usage: $0 <size_mb> <partitioned:on|off>" >&2
  echo "  size_mb: total image size in megabytes" >&2
  echo "  partitioned: on to create 20MB FAT32 + unformatted remainder" >&2
}

if [[ ${#} -lt 1 || ${#} -gt 2 ]]; then
  usage
  exit 1
fi

size_mb="$1"
partitioned="${2:-off}"

if ! [[ "$size_mb" =~ ^[0-9]+$ ]]; then
  echo "Error: size_mb must be an integer." >&2
  exit 1
fi

if [[ "$partitioned" == "on" || "$partitioned" == "true" || "$partitioned" == "1" ]]; then
  partitioned="on"
else
  partitioned="off"
fi

if [[ "$partitioned" == "on" && "$size_mb" -le 20 ]]; then
  echo "Error: size_mb must be > 20 for partitioned images." >&2
  exit 1
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
# Place disk image at the CapazOS root (two levels up from repo root).
out_dir="$(cd "$repo_root/../.." && pwd)"
img_path="$out_dir/disk.img"

mkdir -p "$out_dir"

cleanup() {
  if [[ -n "${attached_device:-}" ]]; then
    hdiutil detach "$attached_device" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT

if [[ -f "$img_path" ]]; then
  rm -f "$img_path"
fi

dd if=/dev/zero of="$img_path" bs=1m count="$size_mb" status=none

attached_device="$(hdiutil attach -imagekey diskimage-class=CRawDiskImage -nomount "$img_path" | awk 'NR==1{print $1}')"
if [[ -z "$attached_device" ]]; then
  echo "Error: failed to attach image." >&2
  exit 1
fi

if [[ "$partitioned" == "on" ]]; then
  diskutil partitionDisk "$attached_device" 2 MBR "MS-DOS FAT32" CAPAZ 20M "Free Space" "" R >/dev/null
else
  newfs_msdos -F 32 -v CAPAZ "$attached_device" >/dev/null
fi

hdiutil detach "$attached_device" >/dev/null 2>&1 || true
attached_device=""

mount_point=""
for _ in 1 2 3 4 5; do
  attach_out="$(hdiutil attach -nobrowse -imagekey diskimage-class=CRawDiskImage "$img_path")"
  mount_point="$(echo "$attach_out" | awk 'index($3, "/Volumes/") == 1 {print $3; exit}')"
  attached_device="$(echo "$attach_out" | awk 'NR==1{print $1}')"
  if [[ -n "$mount_point" && -d "$mount_point" ]]; then
    break
  fi
  hdiutil detach "$attached_device" >/dev/null 2>&1 || true
  sleep 0.2
done

if [[ -z "$mount_point" || ! -d "$mount_point" ]]; then
  echo "Error: failed to mount FAT32 volume." >&2
  exit 1
fi

mkdir -p "$mount_point/Folder"

printf "x7vQp3!mZ9s\n" > "$mount_point/a.txt"
printf "bX-17kLz!!\n" > "$mount_point/B.TXT"
printf "QwE9@rT2u\n" > "$mount_point/Folder/C.txt"

sync

exit 0
