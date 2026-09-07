#!/usr/bin/env bash
# Read-only migration and toolchain verification.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OPENVELA_ROOT="${OPENVELA_ROOT:-$(cd "$PROJECT_ROOT/.." && pwd)}"
failures=0

check_command() {
    if command -v "$1" >/dev/null 2>&1; then
        printf '[OK] command: %s\n' "$1"
    else
        printf '[MISSING] command: %s\n' "$1"
        failures=$((failures + 1))
    fi
}

check_file() {
    if [ -f "$1" ]; then
        printf '[OK] file: %s\n' "${1#$PROJECT_ROOT/}"
    else
        printf '[MISSING] file: %s\n' "${1#$PROJECT_ROOT/}"
        failures=$((failures + 1))
    fi
}

if [ "$(uname -s)" != "Linux" ]; then
    printf '[FAIL] expected Linux, found %s\n' "$(uname -s)"
    failures=$((failures + 1))
else
    printf '[OK] operating system: Linux\n'
fi

for cmd in git git-lfs make python3 repo rsync sha256sum tar; do
    check_command "$cmd"
done

printf '[INFO] project root: %s\n' "$PROJECT_ROOT"
printf '[INFO] OpenVela root: %s\n' "$OPENVELA_ROOT"
printf '[INFO] CPU threads: %s (recommended JOBS=2 on the target 8 GB host)\n' "$(nproc 2>/dev/null || printf '?')"
free -h 2>/dev/null | sed -n '1,2p' || true
df -h "$OPENVELA_ROOT" 2>/dev/null | sed -n '1,2p' || true

for path in \
    "$PROJECT_ROOT/.git/HEAD" \
    "$OPENVELA_ROOT/nuttx/Makefile" \
    "$OPENVELA_ROOT/packages/ai_agent/fix_esp32s3.sh" \
    "$OPENVELA_ROOT/prebuilts/gcc/linux-x86_64/xtensa-esp32s3-elf/bin/xtensa-esp32s3-elf-gcc"; do
    check_file "$path"
done

for path in \
    "$PROJECT_ROOT/artifacts/nuttx.bin" \
    "$PROJECT_ROOT/artifacts/nuttx.elf" \
    "$PROJECT_ROOT/artifacts/SHA256SUMS" \
    "$PROJECT_ROOT/tools/staging-network_manager.c" \
    "$PROJECT_ROOT/tools/staging-vela_tls.c" \
    "$PROJECT_ROOT/tools/staging-http_proxy.c"; do
    check_file "$path"
done

if [ -f "$PROJECT_ROOT/artifacts/SHA256SUMS" ]; then
    if (cd "$PROJECT_ROOT" && sha256sum -c artifacts/SHA256SUMS); then
        printf '[OK] firmware hashes\n'
    else
        printf '[FAIL] firmware hashes\n'
        failures=$((failures + 1))
    fi
fi

if [ -x "$PROJECT_ROOT/.venv/bin/python" ]; then
    if "$PROJECT_ROOT/.venv/bin/python" -m esptool version; then
        printf '[OK] project esptool\n'
    else
        printf '[FAIL] project esptool\n'
        failures=$((failures + 1))
    fi
else
    printf '[MISSING] .venv; run ./scripts/setup_ubuntu.sh\n'
    failures=$((failures + 1))
fi

if [ -f "$PROJECT_ROOT/.git/HEAD" ]; then
    printf '[INFO] contest repository branch: %s\n' \
        "$(git -C "$PROJECT_ROOT" branch --show-current 2>/dev/null || printf '?')"
    printf '[INFO] contest repository commit: %s\n' \
        "$(git -C "$PROJECT_ROOT" rev-parse --short HEAD 2>/dev/null || printf '?')"
fi

if [ "$failures" -ne 0 ]; then
    printf '\nVerification failed: %d issue(s).\n' "$failures"
    exit 1
fi

printf '\nMigration verification passed.\n'
