#!/bin/sh
# 刷写后的结构验证：audio_stream 看 [RXNORM]/[STRM]，再跑一次 voice 产生上传件供离线取证。
cd /home/hfy/_diag || exit 1
export PATH="/usr/bin:/bin:$PATH"

python3 tr_plan.py 60 \
  "5:kws_listen stop" \
  "8:audio_stream 2" \
  "20:voice" \
  "50:config_show" \
  > /tmp/rxnorm_verify.log 2>&1
echo "TRPLAN_RC=$?"

echo "=== 关键行 ==="
grep -E ">>>|STRM\]|RXNORM|RAW\]|Voice|POST|rate|asr|ERR" /tmp/rxnorm_verify.log | head -60
echo "=== 成败 ==="
grep -E "获取 shell 失败" /tmp/rxnorm_verify.log || echo "shell ok"
