#!/usr/bin/env bash

# Backwards-compatibility shim.
#
# Historically the build scripts sourced Scripts/lib/buildinfo.sh.
# The implementation now lives in Scripts/buildinfo.sh.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "${SCRIPT_DIR}/buildinfo.sh"
