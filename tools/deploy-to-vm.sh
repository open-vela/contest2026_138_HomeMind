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
NUTTX_I2S_PATCH="$PROJECT_ROOT/firmware/patches/0004-homemind-esp32s3-i2s-audio-buffer-info.patch"
NUTTX_I2S_WS_PATCH="$PROJECT_ROOT/firmware/patches/0005-homemind-esp32s3-i2s-rx-ws-width.patch"
NUTTX_I2S_DBG_PATCH="$PROJECT_ROOT/firmware/patches/0006-homemind-esp32s3-i2s-rx-regdump.patch"
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
[ -f "$NUTTX_I2S_DBG_PATCH" ] || fail "NuttX I2S regdump patch not found: $NUTTX_I2S_DBG_PATCH"
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
  src/vision/kws_model_data.cc
  src/vision/kws_model_data.h
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

# 0004/0005 记录的是"先手工改 live 树、再回填成补丁"的改动。这里**不能**
# 用 apply_optional_nuttx_patch：live 树被手工改过后 `patch --forward` 的
# dry-run 反而会成功，于是同一个 hunk 被重复打进文件 —— 2026-09-16 实测把
# 0004 打了三份（显式 deploy 一次 + build 内部又 deploy 一次），
# esp32s3_i2s.c 出现 duplicate case，编译直接失败。改成"先看标记、缺了才
# 打、打完再验标记"的幂等逻辑。
I2S_DRIVER="$OPENVELA_ROOT/nuttx/arch/xtensa/src/esp32s3/esp32s3_i2s.c"

ensure_marker_patch() {
    local label="$1"
    local patch_file="$2"
    local marker="$3"
    local target="$4"

    [ -f "$patch_file" ] || fail "patch not found: $patch_file"
    [ -f "$target" ] || fail "patch target not found: $target"

    if grep -q "$marker" "$target"; then
        printf '[INFO] %s already present\n' "$label"
        return 0
    fi

    printf '[INFO] Applying %s\n' "$label"
    patch --batch --forward -d "$OPENVELA_ROOT/nuttx" -p1 --silent \
        < "$patch_file" || fail "could not apply $label"
    grep -q "$marker" "$target" ||
        fail "$label reported success but the marker is missing in $target"
    printf '[INFO] %s applied and verified\n' "$label"
}

ensure_marker_patch "HomeMind I2S audio buffer info" "$NUTTX_I2S_PATCH" \
    "AUDIOIOC_GETBUFFERINFO" "$I2S_DRIVER"
ensure_marker_patch "HomeMind I2S RX WS width" "$NUTTX_I2S_WS_PATCH" \
    "HomeMind WS width fix" "$I2S_DRIVER"
ensure_marker_patch "HomeMind I2S RX regdump" "$NUTTX_I2S_DBG_PATCH" \
    "HM_I2S_RX_DIAG" "$I2S_DRIVER"

# 兜底：case 必须恰好 1 个，重复注入会直接毁掉编译。
n_i2s_case=$(grep -c "case AUDIOIOC_GETBUFFERINFO" "$I2S_DRIVER" || true)
[ "$n_i2s_case" = "1" ] ||
    fail "expected exactly one AUDIOIOC_GETBUFFERINFO case, found $n_i2s_case"

# 同理：0006 的诊断块只准出现一次。
n_i2s_dbg=$(grep -c "/HM_I2S_RX_DIAG" "$I2S_DRIVER" || true)
[ "$n_i2s_dbg" = "1" ] ||
    fail "expected exactly 1 closing HM_I2S_RX_DIAG marker, found $n_i2s_dbg"
printf '[INFO] I2S driver patches verified\n'

install -D -m 0644 "$NUTTX_MEDIA_SOURCE" "$NUTTX_MEDIA_DEST"
printf '[INFO] Installed EYE camera board source\n'

printf '[INFO] HomeMind sources are installed\n'
