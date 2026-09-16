#!/bin/bash
# 刷写 RX 归一化固件：先让网关松开串口，刷完再拉起。
R=/home/hfy/work/openvela-clean-20260830/contest2026_138_HomeMind
cd "$R" || exit 1
export PATH="/usr/bin:/bin:$PATH"

echo "=== 停网关（它占着 /dev/ttyACM1）==="
echo 123456 | sudo -S systemctl stop homemind-gateway 2>&1 | tail -2
sleep 1
(sudo -n true 2>/dev/null && true) || true
fuser -v /dev/ttyACM1 2>&1 | head -5

echo "=== 刷写 ==="
SERIAL_PORT=/dev/homemind-esp32 \
ESPTOOL_PYTHON="$(command -v python3)" \
JOBS=2 ./scripts/build.sh flash 2>&1 | tail -25
frc=${PIPESTATUS[0]}
echo "FLASH_RC=$frc"

echo "=== 拉起网关 ==="
echo 123456 | sudo -S systemctl start homemind-gateway 2>&1 | tail -2
sleep 1
systemctl is-active homemind-gateway
echo "DONE_FLASH"
