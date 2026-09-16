#!/usr/bin/env bash
# HomeMind: 聚焦四组 RX 配置。关键自变量只有一个：TOT_CHAN_NUM。
#   A totchan=1 : 帧长 2×16=32 BCLK，WS 高 16 / 低 16 —— 教科书 Philips I2S
#   B totchan=1 + half_bits=7 : 只动 HALF_SAMPLE_BITS（slot 位宽保持 16）
#   C totchan=0 : 现状基线（帧长 16 BCLK，WS 恒高）
#   D totchan=3 : 帧长 64 BCLK（fs 变 8k），作为"WS 占空比"的对照
# 只改 TOT_CHAN 与 HALF，bitsmod/chanbits 一律保持 -1（=15），
# 上一轮把它们设成 7 把 slot 变成 8 位，直接把信号打没了。
set -u
cd /home/hfy/_diag
python3 tr_plan.py 165 \
  "8:kws_listen stop" \
  "15:i2sknob -1 -1 -1 -1 1" \
  "18:audio_stream 3" \
  "42:i2sknob 7 -1 -1 -1 1" \
  "45:audio_stream 3" \
  "69:i2sknob -1 -1 -1 -1 0" \
  "72:audio_stream 3" \
  "96:i2sknob -1 -1 -1 -1 3" \
  "99:audio_stream 3" \
  "130:audio_stream 3" \
  2>&1 | grep -E "I2SDBG rxstart|I2SKNOB|STRM|Audio\] DONE|acq|>>>|END"
