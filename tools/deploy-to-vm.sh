#!/usr/bin/env bash
# Install the exact HomeMind ai_agent overlay and NuttX patch into a synced
# OpenVela workspace. The historical filename is kept for existing commands.

set -euo pipefail

OPENVELA_ROOT="${1:-$HOME/work/openvela}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
AI_AGENT_ROOT="$OPENVELA_ROOT/packages/ai_agent"
AI_AGENT_OVERLAY="$PROJECT_ROOT/firmware/ai_agent_overlay"
NUTTX_PATCH="$PROJECT_ROOT/firmware/patches/0001-homemind-esp32s3-nuttx.patch"

fail() {
    printf '[ERROR] %s\n' "$*" >&2
    exit 1
}

[ -d "$AI_AGENT_ROOT/src" ] || fail "ai_agent not found: $AI_AGENT_ROOT"
[ -f "$OPENVELA_ROOT/nuttx/Makefile" ] || fail "NuttX not found: $OPENVELA_ROOT/nuttx"
[ -f "$NUTTX_PATCH" ] || fail "NuttX patch not found: $NUTTX_PATCH"

overlay_files=(
    Makefile
    include/tools/tool_device.h
    include/tools/tool_led.h
    include/ui/hm_lcd_display.h
    src/agent_main.c
    src/channels/cmd_llm.c
    src/channels/nsh_commands.c
    src/core/agent_loop.c
    src/infra/config_store.c
    src/infra/http_proxy.c
    src/infra/network_manager.c
    src/infra/vela_tls.c
    src/tools/tool_device.c
    src/tools/tool_led.c
    src/tools/tool_registry.c
    src/ui/hm_lcd_display.c
)

printf '[INFO] Installing HomeMind ai_agent overlay\n'
for rel in "${overlay_files[@]}"; do
    src="$AI_AGENT_OVERLAY/$rel"
    dst="$AI_AGENT_ROOT/$rel"
    [ -f "$src" ] || fail "Overlay file missing: $src"
    install -D -m 0644 "$src" "$dst"
    printf '  %s\n' "$rel"
done

printf '[INFO] Checking HomeMind NuttX patch\n'
if patch --batch --forward -d "$OPENVELA_ROOT/nuttx" -p1 --dry-run --silent < "$NUTTX_PATCH"; then
    patch --batch --forward -d "$OPENVELA_ROOT/nuttx" -p1 --silent < "$NUTTX_PATCH"
    printf '[INFO] NuttX patch applied\n'
elif patch --batch --reverse -d "$OPENVELA_ROOT/nuttx" -p1 --dry-run --silent < "$NUTTX_PATCH"; then
    printf '[INFO] NuttX patch already applied\n'
else
    fail "NuttX source differs from both the recorded base and HomeMind patch; stop and inspect it"
fi

printf '[INFO] HomeMind sources are installed\n'
