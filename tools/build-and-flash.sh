#!/usr/bin/env bash
# build-and-flash.sh — Legacy Ubuntu build helper for ai_agent
#
# Usage:
#   On Ubuntu: bash build-and-flash.sh [openvela_root]
# Prefer ../scripts/build.sh for new work.
#
# This script handles the full build pipeline:
# 1. Apply HomeMind modifications
# 2. Build ai_agent firmware
# 3. Report build status

set -uo pipefail

OPENVELA_ROOT="${1:-$HOME/work/openvela}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
LOG_DIR="$OPENVELA_ROOT/logs/homemind"

mkdir -p "$LOG_DIR"

echo "=== HomeMind Build Script ==="
echo "OpenVela root: $OPENVELA_ROOT"
echo "Log dir: $LOG_DIR"
echo ""

# Step 0: Apply HomeMind modifications
echo "Step 0: Applying HomeMind modifications..."
if [ -f "$SCRIPT_DIR/deploy-to-vm.sh" ]; then
    bash "$SCRIPT_DIR/deploy-to-vm.sh" "$OPENVELA_ROOT" 2>&1 | tee "$LOG_DIR/deploy.log"
else
    echo "WARNING: deploy-to-vm.sh not found, using existing source"
fi

# Step 1: Set up environment
echo ""
echo "Step 1: Setting up build environment..."
cd "$OPENVELA_ROOT"

export CCACHE_DISABLE=1
export PATH="$HOME/.local/bin:$OPENVELA_ROOT/prebuilts/build-tools/linux-x86_64/bin:$OPENVELA_ROOT/prebuilts/gcc/linux-x86_64/xtensa-esp32s3-elf/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
export LD_LIBRARY_PATH="$OPENVELA_ROOT/prebuilts/build-tools/linux-x86_64/lib:${LD_LIBRARY_PATH:-}"
export CROSSDEV="xtensa-esp32s3-elf-"
export CONFIG_STACK_USAGE_WARNING=0

# Step 2: Run fix_esp32s3.sh in background
echo "Step 2: Running fix_esp32s3.sh in background..."
if [ -f "$OPENVELA_ROOT/packages/ai_agent/fix_esp32s3.sh" ]; then
    bash "$OPENVELA_ROOT/packages/ai_agent/fix_esp32s3.sh" > "$LOG_DIR/fix.log" 2>&1 &
    fix_pid=$!
else
    echo "WARNING: fix_esp32s3.sh not found"
    fix_pid=0
fi

# Step 3: Build
echo "Step 3: Building ai_agent..."
build_start=$(date +%s)

set +e
make -C nuttx -j4 > "$LOG_DIR/build.log" 2>&1
build_rc=$?
set -e

build_end=$(date +%s)
build_duration=$((build_end - build_start))

if [ "$build_rc" -eq 0 ]; then
    echo "BUILD SUCCESS (${build_duration}s)"
    echo "ELF: $OPENVELA_ROOT/nuttx/nuttx.elf"
else
    echo "BUILD FAILED (rc=$build_rc, ${build_duration}s)"
    echo "See: $LOG_DIR/build.log"
    exit "$build_rc"
fi

# Wait for fix script
if [ "$fix_pid" -gt 0 ]; then
    wait "$fix_pid" 2>/dev/null || true
fi

# Step 4: Create artifacts directory
echo ""
echo "Step 4: Creating artifacts..."
ARTIFACTS_DIR="$SCRIPT_DIR/../artifacts"
mkdir -p "$ARTIFACTS_DIR"

cp "$OPENVELA_ROOT/nuttx/nuttx.elf" "$ARTIFACTS_DIR/nuttx.elf"
echo "Copied nuttx.elf to $ARTIFACTS_DIR/"

# Step 5: Generate bin file (if esptool available)
if command -v esptool.py &>/dev/null || command -v esptool &>/dev/null; then
    echo "Step 5: Generating nuttx.bin..."
    esptool_cmd=$(command -v esptool.py || command -v esptool)
    $esptool_cmd --chip esp32s3 elf2image "$ARTIFACTS_DIR/nuttx.elf" -o "$ARTIFACTS_DIR/nuttx.bin" 2>&1 | tee "$LOG_DIR/elf2image.log"
    echo "Generated: $ARTIFACTS_DIR/nuttx.bin"
else
    echo "Step 5: esptool not found, skipping elf2image"
    echo "Install the project environment with scripts/setup_ubuntu.sh, then rerun."
fi

echo ""
echo "=== Build Complete ==="
echo ""
echo "Next steps:"
echo "  1. Close any serial terminal"
echo "  2. Run: SERIAL_PORT=/dev/ttyACM0 ./scripts/build.sh flash"
echo "  3. Open a 115200-baud serial terminal"
echo "  4. Run: nsh> ai_agent"
echo "  5. Configure Wi-Fi and MiMo only at the device CLI"
