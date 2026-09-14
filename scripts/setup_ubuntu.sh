#!/usr/bin/env bash
# Install host-side dependencies for native Ubuntu development.
# This script never stores Wi-Fi credentials or API tokens.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CONFIGURE_UDEV=0

if [ "${1:-}" = "--configure-udev" ]; then
    CONFIGURE_UDEV=1
elif [ -n "${1:-}" ] && [ "${1:-}" != "--help" ]; then
    printf 'Unknown option: %s\n' "$1" >&2
    exit 2
elif [ "${1:-}" = "--help" ]; then
    printf 'Usage: ./scripts/setup_ubuntu.sh [--configure-udev]\n'
    exit 0
fi

if [ "$(uname -s)" != "Linux" ]; then
    printf 'ERROR: This script must run on Linux/Ubuntu.\n' >&2
    exit 1
fi

sudo apt-get update
sudo apt-get install -y \
    autoconf automake build-essential bison ca-certificates ccache cmake curl \
    flex gperf git git-lfs kconfig-frontends libc++abi-dev libffi-dev libssl-dev \
    libtool libusb-1.0-0-dev ninja-build pkg-config python3 python3-pip \
    python3-venv repo unzip xz-utils zip

git lfs install
python3 -m venv "$PROJECT_ROOT/.venv"
"$PROJECT_ROOT/.venv/bin/python" -m pip install --upgrade pip
"$PROJECT_ROOT/.venv/bin/python" -m pip install esptool==5.3.1 pyserial

if ! id -nG "$USER" | grep -qw dialout; then
    sudo usermod -aG dialout "$USER"
    printf 'Added %s to dialout; log out and back in before using the serial port.\n' "$USER"
fi

if [ "$CONFIGURE_UDEV" -eq 1 ]; then
    sudo install -m 0644 "$PROJECT_ROOT/tools/99-homemind-esp32.rules" \
        /etc/udev/rules.d/99-homemind-esp32.rules
    sudo udevadm control --reload-rules
    sudo udevadm trigger
fi

printf '\nUbuntu dependencies are ready.\n'
printf 'Next: bash %s/tools/verify-ubuntu.sh\n' "$PROJECT_ROOT"
