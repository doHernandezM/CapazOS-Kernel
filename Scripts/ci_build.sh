#!/usr/bin/env bash
set -euo pipefail

# CI/parity wrapper: match Xcode invocation as closely as possible.
# Usage:
#   ./ci_build.sh [--platform aarch64-virt] [--config debug] [--out <dir>]

SCRIPTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "${SCRIPTS_DIR}/build.sh" --platform aarch64-virt "$@"
