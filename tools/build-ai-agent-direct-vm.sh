#!/usr/bin/env bash

set -uo pipefail

root_dir="${1:-$HOME/work/openvela}"
log_dir="$root_dir/logs/homemind"
status_file="$log_dir/ai-agent-direct-build.status"

mkdir -p "$log_dir"
cd "$root_dir"

export CCACHE_DISABLE=1
export PATH="$HOME/.local/bin:$root_dir/prebuilts/build-tools/linux-x86_64/bin:$root_dir/prebuilts/gcc/linux-x86_64/xtensa-esp32s3-elf/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
export LD_LIBRARY_PATH="$root_dir/prebuilts/build-tools/linux-x86_64/lib:${LD_LIBRARY_PATH:-}"
export CROSSDEV="xtensa-esp32s3-elf-"
export CONFIG_STACK_USAGE_WARNING=0
# Do not inject a proxy. Intentionally exported proxy variables, if any,
# are inherited from the operator's environment.

printf 'running\n' > "$status_file"

bash packages/ai_agent/fix_esp32s3.sh > "$log_dir/ai-agent-direct-fix.log" 2>&1 &
fix_pid=$!

set +e
make -C nuttx -j4 > "$log_dir/ai-agent-direct-build.log" 2>&1
build_rc=$?

if [ "$build_rc" -eq 0 ]; then
  wait "$fix_pid"
  fix_rc=$?
else
  kill "$fix_pid" 2>/dev/null || true
  wait "$fix_pid" 2>/dev/null || true
  fix_rc=125
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
