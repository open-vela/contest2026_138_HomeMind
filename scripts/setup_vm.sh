#!/usr/bin/env bash
# Compatibility entry point. The project now targets native Ubuntu.

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec bash "$SCRIPT_DIR/setup_ubuntu.sh" "$@"
