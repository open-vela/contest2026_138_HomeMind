# 2026-09-10 音频工作包 D 进展

固件：`e555a0cf…`（含 `audio_stream`）

## 已完成

1. 新增串口命令 **`audio_stream [sec]`**（`nsh_commands.c`）。
2. 实现 `hm_audio_mb_record()`：一次 open + 4×640B 入队 + START + poll。
3. 统计输出：`bytes/sessions/peak/rms`，PASS/PARTIAL/FAIL 门限。

## 实测

| 路径 | 结果 |
| --- | --- |
| `hm_voice_record`（每 640B 开-关） | 仅 **640B** 后 break（`FAIL short`） |
| 多缓冲 4×640 入队 + START | 打印 `enqueued 4 x 640B` 后 **卡住**（无 DONE；hard-reset 恢复） |

与 09-03 结论一致：I2S 下半层在 START/多缓冲下不可靠；**单块 640B 有限采样仍为唯一稳定形态**。

## 仍阻塞（离线唤醒前置）

- 连续 PCM 流未验收 → 指定词「你好，openvela」与 LED 语音命令 **未实现**。
- `media_recorder.h` / 完整 multimedia 栈不在当前 clean 工作区，`audio_capture.c` 的 media_recorder 后端无法编。
- `CONFIG_MEDIA=y` 会连锁打开 `tool_media.c` 且缺 `media_player.h`，已回退。

## 建议下一步

1. 深挖 ESP32-S3 I2S RX 多缓冲 DMA 生命周期（drivers/audio / esp32s3_i2s）；
2. 或在「640B×N 次开-关」上做 VAD/特征（有间隙，仅作演示）；
3. KWS 模型（TFLM micro_speech）依赖稳定 ≥1s 窗口，先修驱动。

命令：`ai_agent` → `audio_stream 3`  
日志备份：`/home/hfy/work/backups-20260910/nsh_commands.c.*`
