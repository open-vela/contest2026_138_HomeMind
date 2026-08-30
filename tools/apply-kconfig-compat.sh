#!/usr/bin/env bash

set -euo pipefail

root_dir="${1:-$HOME/work/openvela}"

sed -i 's|^osource "\$APPSDIR/external/zblue/|# osource "$APPSDIR/external/zblue/|' \
  "$root_dir/external/zblue/Kconfig"
sed -i '\|^source ".*/external/ril/Kconfig"|s|^|# |' \
  "$root_dir/external/Kconfig"
sed -i 's|^source "drivers/hwtracing/tricoreht/Kconfig"|# source "drivers/hwtracing/tricoreht/Kconfig"|' \
  "$root_dir/nuttx/drivers/hwtracing/Kconfig"
