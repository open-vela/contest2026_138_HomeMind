# 2026-09-10 I2S RX 连续采集突破

固件 BIN：`dc4af2ebe034c9781fffd1486f1ff7cd612dbd3f7bca5f68fd2d54b69ab9f1dc`

## 结果

```
audio_stream 3
[Audio] DONE bytes=96000/96000 peak=16384 rms=74.8
[Audio] PASS continuous
```

- 150 × 640B = **3 秒** @16kHz/16bit/mono
- 每块 I2S-RX IRQ `match=1`，worker 正常回调
- 命令：`ai_agent` → `audio_stream 3`

## 根因（两层）

1. **`audio_poll` 是环形 head/tail**（`nuttx/audio/audio.c`）：第一块完成后 poll 立刻返回 `POLLIN|POLLERR`，后续块 `nbytes` 仍为 0 → 误以为 DMA 失败。
2. **不能在 400ms 内靠 poll 等第 2 块**；也不是 I2S 硬件坏。驱动诊断显示第 2 块 DMA/IRQ 实际会在 STOP 之后才到（竞态）。

## 可用配方（已进 `hm_audio_stream_session`）

```
open → CONFIGURE
loop:
  ALLOC 640B → ENQUEUE → START（仅首块）
  首块: poll(800ms)
  其后: usleep(30000)   # 640B@16k ≈20ms
  若 apb->nbytes>0 则 memcpy
  FREE buffer
STOP → close
```

诊断打印：`esp32s3_i2s.c` 中 `[I2S-RX] setup/start/irq/worker`（验收前可关）。

## 仍待

- 去掉驱动 printf、做 ≥1s 稳定窗口与丢块统计
- 能量 VAD / 指定词 KWS（依赖本连续路径）
- `media_recorder` 后端仍不可用（工作区无头文件）

证据脚本：`transfer/patch_audio_session.py`、`patch_i2s_diag2.py`
备份：`/home/hfy/work/backups-20260910/`
