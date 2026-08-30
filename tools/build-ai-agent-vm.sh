#!/usr/bin/env bash

set -uo pipefail

root_dir="${1:-$HOME/work/openvela}"
log_dir="$root_dir/logs/homemind"
status_file="$log_dir/ai-agent-build.status"

mkdir -p "$log_dir"
cd "$root_dir"

export CCACHE_DISABLE=1
export PATH="$root_dir/prebuilts/gcc/linux-x86_64/xtensa-esp32s3-elf/bin:$PATH"
export CROSSDEV="xtensa-esp32s3-elf-"
export CONFIG_STACK_USAGE_WARNING=0

printf 'running\n' > "$status_file"

# configure.sh refreshes the external tree before Kconfig parsing. The
# competition branch's prebuilt parser does not understand optional osource,
# so keep the two disabled zblue includes commented during that short window.
(
  for _ in $(seq 1 300); do
    sed -i 's|^osource "\$APPSDIR/external/zblue/|# osource "$APPSDIR/external/zblue/|' \
      external/zblue/Kconfig 2>/dev/null || true
    sed -i '\|^source ".*/external/ril/Kconfig"|s|^|# |' \
      external/Kconfig 2>/dev/null || true
    sed -i 's|^source "drivers/hwtracing/tricoreht/Kconfig"|# source "drivers/hwtracing/tricoreht/Kconfig"|' \
      nuttx/drivers/hwtracing/Kconfig 2>/dev/null || true
    sleep 0.2
  done
) > "$log_dir/kconfig-compat.log" 2>&1 &
compat_pid=$!

bash packages/ai_agent/fix_esp32s3.sh > "$log_dir/ai-agent-fix.log" 2>&1 &
fix_pid=$!

set +e
./build.sh esp32s3-eye:ai_agent > "$log_dir/ai-agent-build.log" 2>&1
build_rc=$?
kill "$compat_pid" 2>/dev/null || true
wait "$compat_pid" 2>/dev/null || true

if [ "$build_rc" -ne 0 ]; then
  kill "$fix_pid" 2>/dev/null || true
  wait "$fix_pid" 2>/dev/null || true
  fix_rc=125
else
  wait "$fix_pid"
  fix_rc=$?
fi
set -e

{
  printf 'build_rc=%s\n' "$build_rc"
  printf 'fix_rc=%s\n' "$fix_rc"
} > "$status_file"

if [ "$build_rc" -ne 0 ]; then
  exit "$build_rc"
fi

exit "$fix_rc"
