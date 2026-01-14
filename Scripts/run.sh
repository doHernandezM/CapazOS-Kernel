#!/usr/bin/env bash
set -euo pipefail

THIS="${BASH_SOURCE[0]}"
SCRIPTS_DIR="$(cd "$(dirname "$THIS")" && pwd)"
KERNEL_DIR="$(cd "${SCRIPTS_DIR}/.." && pwd)"
CODE_DIR="$(cd "${KERNEL_DIR}/.." && pwd)"

# Workspace layout
#   <workspace>/Code/Kernel/Scripts -> build products in <workspace>/build
#   legacy: <workspace>/Kernel/Scripts -> build products in <workspace>/build
if [[ "$(basename "${CODE_DIR}")" == "Code" ]]; then
  WORKSPACE_ROOT="$(cd "${CODE_DIR}/.." && pwd)"
else
  WORKSPACE_ROOT="${CODE_DIR}"
fi

KERNEL_IMG="${WORKSPACE_ROOT}/build/kernel.img"
DTB="${KERNEL_DIR}/Resources/virt.dtb"

if [[ ! -f "${KERNEL_IMG}" ]]; then
  echo "Missing ${KERNEL_IMG}. Build first: ./Scripts/build.sh"
  exit 1
fi

qemu-system-aarch64 \
  -machine virt \
  -cpu cortex-a72 \
  -m 128M \
  -nographic \
  -serial mon:stdio \
  -kernel "${KERNEL_IMG}" \
  -dtb "${DTB}"
