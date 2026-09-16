#!/bin/sh
# 零刷写扫描：I2S RX 帧宽相关旋钮 totchan，看 [STRM] eff_rate 是否随帧率变化。
# 假设：当前帧是 4 slot(64bit) => WS=8kHz => 字率约 8000/s；
#       若 totchan 能改回 2 slot(32bit) => WS=16kHz => eff_rate 应翻倍。
cd /home/hfy/_diag || exit 1
export PATH="/usr/bin:/bin:$PATH"

echo "=== port holders ==="
(command -v fuser >/dev/null && fuser -v /dev/homemind-esp32 /dev/ttyACM1 2>&1) | head -10
echo "=== start ==="

python3 tr_plan.py 70 \
  "5:kws_listen stop" \
  "8:i2sknob -1 -1 -1 -1 -1 0" \
  "10:audio_stream 2" \
  "23:i2sknob -1 -1 -1 15 0 0" \
  "25:audio_stream 2" \
  "36:i2sknob -1 -1 -1 15 1 0" \
  "38:audio_stream 2" \
  "49:i2sknob -1 -1 -1 15 3 0" \
  "51:audio_stream 2" \
  "62:i2sknob -1 -1 -1 15 -1 0" \
  "66:kws_listen status" \
  > /tmp/totchan_sweep.log 2>&1
echo "TRPLAN_RC=$?"

echo "=== 命令与 STRM 摘要 ==="
grep -E ">>>|STRM\]|I2SDBG|knobs |RAW\]|ERR" /tmp/totchan_sweep.log | head -120
echo "=== 成败 ==="
grep -cE "^\[acq\]" /tmp/totchan_sweep.log
grep -E "获取 shell 失败" /tmp/totchan_sweep.log
