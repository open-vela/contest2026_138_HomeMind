#!/usr/bin/env bash
# HomeMind native-Ubuntu build and flash entry point.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OPENVELA_ROOT="${OPENVELA_ROOT:-$(cd "$PROJECT_ROOT/.." && pwd)}"
SERIAL_PORT="${SERIAL_PORT:-/dev/ttyACM0}"
ESPTOOL_PYTHON="${ESPTOOL_PYTHON:-$PROJECT_ROOT/.venv/bin/python}"
# The target Ubuntu host has 8 GB RAM. Start conservatively; override with
# JOBS=3 or JOBS=4 only after a successful baseline build.
JOBS="${JOBS:-2}"
ACTION="${1:-help}"

info() { printf '[INFO] %s\n' "$*"; }
error() { printf '[ERROR] %s\n' "$*" >&2; }

require_file() {
    if [ ! -f "$1" ]; then
        error "Required file not found: $1"
        exit 1
    fi
}

check_environment() {
    if [ "$(uname -s)" != "Linux" ]; then
        error "This entry point must run on Linux/Ubuntu."
        exit 1
    fi
    command -v python3 >/dev/null || { error "python3 is not installed"; exit 1; }
    command -v make >/dev/null || { error "make is not installed"; exit 1; }
    require_file "$OPENVELA_ROOT/nuttx/Makefile"
    require_file "$OPENVELA_ROOT/packages/ai_agent/fix_esp32s3.sh"
    require_file "$PROJECT_ROOT/tools/deploy-to-vm.sh"
    info "Project root: $PROJECT_ROOT"
    info "OpenVela root: $OPENVELA_ROOT"
}

deploy_sources() {
    check_environment
    bash "$PROJECT_ROOT/tools/deploy-to-vm.sh" "$OPENVELA_ROOT"
}

apply_network_capacity_config() {
    local nuttx_config="$OPENVELA_ROOT/nuttx/.config"
    require_file "$nuttx_config"
    command -v kconfig-tweak >/dev/null || {
        error "kconfig-tweak is required to apply the HomeMind network config"
        exit 1
    }

    # The board configuration disables dynamic UDP connection allocation and
    # preallocates only eight sockets. ai_agent consumes all eight before DHCP,
    # so dhcpc_open() fails with EAGAIN. Reserve enough UDP connections for the
    # agent plus DHCP/DNS, while keeping the packet IOB pool at its default.
    kconfig-tweak --file "$nuttx_config" \
        --set-val CONFIG_NET_UDP_PREALLOC_CONNS 16
    kconfig-tweak --file "$nuttx_config" \
        --set-val CONFIG_NET_UDP_NWRBCHAINS 64
    kconfig-tweak --file "$nuttx_config" \
        --set-val CONFIG_NET_TCP_NWRBCHAINS 32
    # The ESP32-S3 regression run showed that leaving these experimental
    # receive-path options enabled can prevent even a plain HTTP response from
    # reaching userspace.  They were removed from this script previously, but
    # kconfig-tweak preserves old .config values unless we disable them
    # explicitly, so make the rollback deterministic across incremental builds.
    kconfig-tweak --file "$nuttx_config" \
        --disable CONFIG_NET_TCP_OUT_OF_ORDER
    kconfig-tweak --file "$nuttx_config" \
        --disable CONFIG_NET_TCP_SELECTIVE_ACK
    # Keep the board's proven IOB size.  Raising 64 buffers from 196 to 512
    # bytes permanently consumed about 20 KiB and made the active mbedTLS
    # context compete more aggressively with the Wi-Fi TX path.  Exact
    # ClientHello replay proved that multi-IOB payload content was not the
    # cause of the missing ACKs.
    kconfig-tweak --file "$nuttx_config" \
        --set-val CONFIG_IOB_BUFSIZE 196
    kconfig-tweak --file "$nuttx_config" \
        --set-val CONFIG_IOB_NBUFFERS 64
    # ESP32-S3 takes about 10 seconds to verify the RSA certificate used by
    # TLS 1.2.  With delayed ACK enabled, the ACK for the server flight is
    # held during that CPU-bound interval; the following server Finished is
    # then rejected with duplicate ACKs.  ACK received TCP data immediately.
    kconfig-tweak --file "$nuttx_config" \
        --disable CONFIG_NET_TCP_DELAYED_ACK
    kconfig-tweak --file "$nuttx_config" \
        --enable CONFIG_NET_TCP_WRITE_BUFFERS
    # TLS 1.3 relies on PSA crypto, which needs an entropy source. The NuttX
    # platform entropy poll calls getrandom(), which opens /dev/urandom; the
    # board image ships without it, so psa_crypto_init() fails. Enable the
    # entropy module and /dev/urandom (ESP32 HW TRNG via the ARCH variant).
    kconfig-tweak --file "$nuttx_config" \
        --enable CONFIG_MBEDTLS_ENTROPY_C
    kconfig-tweak --file "$nuttx_config" \
        --enable CONFIG_DEV_URANDOM
    kconfig-tweak --file "$nuttx_config" \
        --enable CONFIG_DEV_URANDOM_ARCH
    # mbedtls_ssl_setup() with the 16 KiB + 16 KiB defaults makes subsequent
    # ESP32-S3 Wi-Fi TCP transmission time out.  Keep enough inbound space for
    # certificate flights while substantially reducing the setup footprint.
    # mbedTLS handshake debug was a temporary -0x7080 diagnostic and is
    # now off for the clean build: debug callbacks printf from arbitrary
    # task contexts and race the USB CDC console read path (observed as
    # the occasional whole-system console freeze after set_llm).
    kconfig-tweak --file "$nuttx_config" \
        --disable CONFIG_MBEDTLS_DEBUG_C
    kconfig-tweak --file "$nuttx_config" \
        --disable CONFIG_MBEDTLS_DEBUG
    # TCP write-buffer trace (ninfo -> printf [TCP-WRB] in
    # tcp_send_buffered.c) ran per send/ACK/retransmit in network-stack
    # context; the TX fault it traced (PSRAM/DMA pool) is fixed, so turn
    # it off to stop async console writes racing the console read path.
    kconfig-tweak --file "$nuttx_config" \
        --disable CONFIG_NET_TCP_WRBUFFER_DEBUG
    # Persistent /data: LittleFS on an on-chip-flash MTD partition.
    # Firmware image ends around 0xFD000; 0x180000..0x280000 is clear of it
    # and within the 4MB flash map.  board_spiflash_init() (bringup) registers
    # /dev/esp32s3flash and mounts /data, force-formatting on first boot, so
    # Wi-Fi/LLM config survives reboot instead of the TMPFS simulation.
    kconfig-tweak --file "$nuttx_config" \
        --enable CONFIG_ESP32S3_MTD
    kconfig-tweak --file "$nuttx_config" \
        --enable CONFIG_ESP32S3_SPIFLASH
    kconfig-tweak --file "$nuttx_config" \
        --enable CONFIG_ESP32S3_SPIFLASH_LITTLEFS
    kconfig-tweak --file "$nuttx_config" \
        --set-val CONFIG_ESP32S3_STORAGE_MTD_OFFSET 0x180000
    kconfig-tweak --file "$nuttx_config" \
        --set-val CONFIG_ESP32S3_STORAGE_MTD_SIZE 0x100000
    # RAMLOG: dmesg-readable syslog ring (16 KiB) for ask-path hang forensics
    # without touching the USB CDC console.
    kconfig-tweak --file "$nuttx_config" \
        --enable CONFIG_RAMLOG
    kconfig-tweak --file "$nuttx_config" \
        --enable CONFIG_RAMLOG_SYSLOG
    # RAMLOG_SYSLOG replaces the tiny SYSLOG_BUFFER channel (196 B);
    # both together exceed the syslog channel limit.
    kconfig-tweak --file "$nuttx_config" \
        --disable CONFIG_SYSLOG_BUFFER
    # default console channel + ramlog channel
    kconfig-tweak --file "$nuttx_config" \
        --set-val CONFIG_SYSLOG_MAX_CHANNELS 2
    # CRITICAL: agent task stacks (16 KiB) land in PSRAM (MM_REGIONS=2
    # heap spans DRAM+PSRAM).  Without this option the spiflash MTD
    # driver calls spi_flash_write/erase directly from such tasks and
    # the cache-suspend window makes their PSRAM stack unreachable ->
    # whole-board freeze on every LittleFS write from the agent.
    kconfig-tweak --file "$nuttx_config" \
        --enable CONFIG_ESP32S3_SPI_FLASH_SUPPORT_PSRAM_STACK
    # GPIO char driver: /dev/gpio0 = onboard LED (GPIO3), /dev/gpio1 = BOOT
    # button.  Used by the led_control local tool.
    kconfig-tweak --file "$nuttx_config" \
        --enable CONFIG_DEV_GPIO
    # Panel check 2026-08-30: with INVCOLOR on, drawn black shows as
    # white (whole screen looks washed out).  This panel does NOT
    # want inversion.
    kconfig-tweak --file "$nuttx_config" \
        --disable CONFIG_LCD_ST7789_INVCOLOR
    kconfig-tweak --file "$nuttx_config" \
        --set-val CONFIG_RAMLOG_BUFSIZE 16384
    # Materialize defaults for newly enabled symbols (children of
    # ESP32S3_MTD / FS_LITTLEFS): the NuttX make does not run
    # olddefconfig itself, so missing CONFIG_ macros break the build.
    (cd "$OPENVELA_ROOT/nuttx" && make olddefconfig > /dev/null 2>&1) || true
    kconfig-tweak --file "$nuttx_config" \
        --set-val CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN 8192
    kconfig-tweak --file "$nuttx_config" \
        --set-val CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN 1024
    # Root cause of the TLS ClientHello "no ACK" regression (confirmed by
    # driver-level logging on 2026-08-27): Wi-Fi TX buffers are allocated
    # by esp_malloc_internal(), which requires internal (DMA-capable) DRAM
    # and rejects PSRAM pointers. The single kmm heap spans internal DRAM +
    # PSRAM, and mbedTLS ssl_setup() allocations exhaust the internal DRAM
    # free blocks, so 454-byte TX buffer requests get PSRAM pointers, are
    # rejected, and esp_wifi_internal_tx() returns ESP_ERR_NO_MEM (0x0101)
    # while the TLS context is alive. The fix is a 16 KiB static internal
    # DMA fallback pool inside esp32s3_wifi_adapter.c (hm_wlan_pool_*).
    # NOTE: CONFIG_XTENSA_IMEM_USE_SEPARATE_HEAP (96 KiB) was tried and
    # hangs NuttX before the console starts on this board because the
    # internal DRAM free area is smaller than 96 KiB (DEBUGASSERT in
    # up_allocate_heap). Keep it explicitly disabled.
    kconfig-tweak --file "$nuttx_config" \
        --disable CONFIG_XTENSA_IMEM_USE_SEPARATE_HEAP
    info "Configured UDP connections: 16; UDP/TCP write chains: 64/32; IOB: 64 x 196; TCP delayed ACK: off; separate IMEM heap: 96 KiB"
}

disable_mbedtls_tls13() {
    local mbedtls_config="$OPENVELA_ROOT/apps/crypto/mbedtls/include/mbedtls/mbedtls_config.h"
    require_file "$mbedtls_config"
    sed -i \
        's@^#define MBEDTLS_SSL_PROTO_TLS1_3$@/* #define MBEDTLS_SSL_PROTO_TLS1_3 */@' \
        "$mbedtls_config"
    if grep -q '^#define MBEDTLS_SSL_PROTO_TLS1_3$' "$mbedtls_config"; then
        error "Failed to disable MBEDTLS_SSL_PROTO_TLS1_3"
        exit 1
    fi
    info "Disabled experimental mbedTLS TLS 1.3; using TLS 1.2 baseline"
}

build_firmware() {
    deploy_sources
    apply_network_capacity_config
    disable_mbedtls_tls13
    export CCACHE_DISABLE=1
    export CROSSDEV="xtensa-esp32s3-elf-"
    export CONFIG_STACK_USAGE_WARNING=0
    export USE_NXTMPDIR_ESP_REPO_DIRECTLY=y
    export PATH="$PROJECT_ROOT/.venv/bin:$HOME/.local/bin:$OPENVELA_ROOT/prebuilts/build-tools/linux-x86_64/bin:$OPENVELA_ROOT/prebuilts/gcc/linux-x86_64/xtensa-esp32s3-elf/bin:$PATH"
    export LD_LIBRARY_PATH="$OPENVELA_ROOT/prebuilts/build-tools/linux-x86_64/lib:${LD_LIBRARY_PATH:-}"

    # The apps/crypto/mbedtls objects do NOT rebuild when .config changes
    # (no dependency tracking), which caused several "config silently not
    # applied" incidents (TLS buffer sizes, debug hooks).  Force-clean them
    # so every build reflects the current configuration.
    rm -f "$OPENVELA_ROOT"/apps/crypto/mbedtls/mbedtls/library/*.o

    mkdir -p "$OPENVELA_ROOT/logs/homemind"
    info "Building with $JOBS jobs..."
    bash "$OPENVELA_ROOT/packages/ai_agent/fix_esp32s3.sh" \
        >"$OPENVELA_ROOT/logs/homemind/fix.log" 2>&1 &
    local fix_pid=$!

    set +e
    make -C "$OPENVELA_ROOT/nuttx" -j"$JOBS" \
        >"$OPENVELA_ROOT/logs/homemind/build.log" 2>&1
    local build_rc=$?
    set -e

    if [ "$build_rc" -ne 0 ]; then
        kill "$fix_pid" 2>/dev/null || true
        wait "$fix_pid" 2>/dev/null || true
        error "Build failed (rc=$build_rc). See $OPENVELA_ROOT/logs/homemind/build.log"
        exit "$build_rc"
    fi
    wait "$fix_pid"

    local elf_source="$OPENVELA_ROOT/nuttx/nuttx.elf"
    if [ ! -f "$elf_source" ]; then
        elf_source="$OPENVELA_ROOT/nuttx/nuttx"
    fi
    require_file "$elf_source"
    cp "$elf_source" "$PROJECT_ROOT/artifacts/nuttx.elf"
    # The OpenVela Makefile supplies the board-specific DIO/40 MHz image
    # parameters. Re-running elf2image without those flags creates a QIO image
    # that enters a watchdog reset loop on ESP32-S3-EYE.
    require_file "$OPENVELA_ROOT/nuttx/nuttx.bin"
    cp "$OPENVELA_ROOT/nuttx/nuttx.bin" "$PROJECT_ROOT/artifacts/nuttx.bin"
    (cd "$PROJECT_ROOT" && sha256sum artifacts/nuttx.bin artifacts/nuttx.elf > artifacts/SHA256SUMS)
    info "Build complete: $PROJECT_ROOT/artifacts/nuttx.bin"
}

flash_firmware() {
    require_file "$PROJECT_ROOT/artifacts/nuttx.bin"
    require_file "$ESPTOOL_PYTHON"
    if [ ! -e "$SERIAL_PORT" ]; then
        error "Serial device not found: $SERIAL_PORT"
        error "Set SERIAL_PORT=/dev/ttyUSB0 if your board uses another device."
        exit 1
    fi
    info "Flashing $SERIAL_PORT at address 0x0..."
    "$ESPTOOL_PYTHON" -m esptool --chip esp32s3 \
        --port "$SERIAL_PORT" --baud 460800 \
        --before default-reset --after hard-reset \
        write-flash 0x0 "$PROJECT_ROOT/artifacts/nuttx.bin"
}

show_help() {
    cat <<'EOF'
Usage: ./scripts/build.sh <check|deploy|build|flash|all>

Environment:
  OPENVELA_ROOT  Full OpenVela workspace (default: project parent)
  SERIAL_PORT    ESP32 serial device (default: /dev/ttyACM0)
  ESPTOOL_PYTHON Python interpreter with esptool installed
  JOBS           Parallel build jobs (default: 2 for an 8 GB host)

Examples:
  OPENVELA_ROOT=$HOME/work/openvela ./scripts/build.sh check
  OPENVELA_ROOT=$HOME/work/openvela JOBS=2 ./scripts/build.sh build
  SERIAL_PORT=/dev/ttyACM0 ./scripts/build.sh flash
EOF
}

case "$ACTION" in
    check) check_environment ;;
    deploy) deploy_sources ;;
    build) build_firmware ;;
    flash) flash_firmware ;;
    all) build_firmware; flash_firmware ;;
    help|-h|--help) show_help ;;
    *) error "Unknown action: $ACTION"; show_help; exit 2 ;;
esac
