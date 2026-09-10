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
NUTTX_MEDIA_PATCH="$PROJECT_ROOT/firmware/patches/0002-homemind-esp32s3-eye-media.patch"
NUTTX_MEDIA_REPAIR_PATCH="$PROJECT_ROOT/firmware/patches/0003-repair-eye-bringup-media-placement.patch"
NUTTX_MEDIA_SOURCE="$PROJECT_ROOT/firmware/nuttx_media/esp32s3_board_camera.c"
NUTTX_MEDIA_DEST="$OPENVELA_ROOT/nuttx/boards/xtensa/esp32s3/esp32s3-eye/src/esp32s3_board_camera.c"

fail() {
    printf '[ERROR] %s\n' "$*" >&2
    exit 1
}

[ -d "$AI_AGENT_ROOT/src" ] || fail "ai_agent not found: $AI_AGENT_ROOT"
[ -f "$OPENVELA_ROOT/nuttx/Makefile" ] || fail "NuttX not found: $OPENVELA_ROOT/nuttx"
[ -f "$NUTTX_PATCH" ] || fail "NuttX patch not found: $NUTTX_PATCH"
[ -f "$NUTTX_MEDIA_PATCH" ] || fail "NuttX media patch not found: $NUTTX_MEDIA_PATCH"
[ -f "$NUTTX_MEDIA_REPAIR_PATCH" ] || fail "NuttX media repair patch not found: $NUTTX_MEDIA_REPAIR_PATCH"
[ -f "$NUTTX_MEDIA_SOURCE" ] || fail "NuttX camera source not found: $NUTTX_MEDIA_SOURCE"

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
  src/vision/person_detect.cc
  src/vision/person_detect_model_data.cc
  src/vision/person_detect_model_data.h
)

printf '[INFO] Installing HomeMind ai_agent overlay\n'
for rel in "${overlay_files[@]}"; do
    src="$AI_AGENT_OVERLAY/$rel"
    dst="$AI_AGENT_ROOT/$rel"
    [ -f "$src" ] || fail "Overlay file missing: $src"
    install -D -m 0644 "$src" "$dst"
    printf '  %s\n' "$rel"
done

apply_nuttx_patch() {
    local label="$1"
    local patch_file="$2"

    printf '[INFO] Checking %s\n' "$label"
    if patch --batch --forward -d "$OPENVELA_ROOT/nuttx" -p1 --dry-run --silent < "$patch_file"; then
        patch --batch --forward -d "$OPENVELA_ROOT/nuttx" -p1 --silent < "$patch_file"
        printf '[INFO] %s applied\n' "$label"
    elif patch --batch --reverse -d "$OPENVELA_ROOT/nuttx" -p1 --dry-run --silent < "$patch_file"; then
        printf '[INFO] %s already applied\n' "$label"
    else
        fail "NuttX source differs from both the recorded base and %s; stop and inspect it" "$label"
    fi
}

apply_optional_nuttx_patch() {
    local label="$1"
    local patch_file="$2"

    printf '[INFO] Checking optional %s\n' "$label"
    if patch --batch --forward -d "$OPENVELA_ROOT/nuttx" -p1 --dry-run --silent < "$patch_file"; then
        patch --batch --forward -d "$OPENVELA_ROOT/nuttx" -p1 --silent < "$patch_file"
        printf '[INFO] %s applied\n' "$label"
    elif patch --batch --reverse -d "$OPENVELA_ROOT/nuttx" -p1 --dry-run --silent < "$patch_file"; then
        printf '[INFO] %s already applied\n' "$label"
    else
        printf '[INFO] %s not needed\n' "$label"
    fi
}

apply_nuttx_patch "HomeMind NuttX patch" "$NUTTX_PATCH"
apply_optional_nuttx_patch "EYE bringup placement repair" "$NUTTX_MEDIA_REPAIR_PATCH"
apply_nuttx_patch "HomeMind EYE media patch" "$NUTTX_MEDIA_PATCH"

install -D -m 0644 "$NUTTX_MEDIA_SOURCE" "$NUTTX_MEDIA_DEST"
printf '[INFO] Installed EYE camera board source\n'

printf '[INFO] HomeMind sources are installed\n'
