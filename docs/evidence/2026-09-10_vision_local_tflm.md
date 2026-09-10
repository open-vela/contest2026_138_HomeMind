# 2026-09-10 端侧 TFLM `vision local` 验收

状态：**已验收（单帧）**。摄像头采集 + TFLM INT8 人员检测端到端打通。

## 结果

| 项目 | 值 |
| --- | --- |
| 固件 BIN | `0764e58fc6b0d38ab85311c6af470cab624c01603a3cddd69e6931f3f41b8696` |
| 命令 | `ai_agent` → `vision local` |
| 模型 | person_detect.tflite INT8，MTD/XIP 加载 magic=TFL3 |
| 初始化 | arena=139264，input=9216，output=2，AllocateTensors OK |
| 取帧 | OV2640 QVGA RGB565，153600 B |
| 推理 | `person=0.594`（首帧）/ `0.543`（次帧复用 init），threshold=0.50 → **DETECTED** |
| 延迟 | 约 4150 ms/次（含取帧+Invoke；init 首次更长） |
| 并行 | `media_probe` 仍可出帧 153600 B |

## 真正卡点（三层叠加）

1. **CLI 64KB DRAM 静态栈**（上一轮）→ `media_probe` 硬锁。已回退 `agent_task_create`。
2. **`deploy-to-vm.sh` 未部署 `src/vision/*`** → 改了 overlay 的 `person_detect.cc` 却一直链旧 9 月 8 日对象，现象是「worker 启动后无任何 PD 日志」。已在 `tools/deploy-to-vm.sh` 的 `overlay_files` 加入三个 vision 文件。
3. **worker 栈上 stdio/littlefs 死锁** → `vprintf`/`fopen` 在 vision worker（32KB DRAM）上会挂死；`write(1)` 正常。`pd_flog` 已改为仅 `write(1)`，TFLM 路径不再写 `/data/pd.log`。

## 架构（当前）

- CLI：PSRAM 栈（`agent_task_create`）— 负责 `media_probe` 等。
- Vision worker：`g_pd_stack[32KB]` DRAM BSS，仅在 `vision local` 期间使用 — 负责 TFLM init/capture/run。
- **禁止**再把整条 CLI 栈放进 DRAM BSS。

## 复用

- 构建后必须确认 packages 中 `person_detect.cc` 时间戳为今天；vision 改动进 `overlay_files`。
- 探测：`media_probe` 期望 `MEDIA_VIDEO_FRAME bytes=153600`；`vision local` 期望 `person=... DETECTED/none`。
- 备份：`/home/hfy/work/backups-20260910/`。

## 仍待

- 20 正/20 负样本统计、安静/噪声/断网分测；
- 诊断打印清理；
- 未 commit/push；
- 次帧音频 PCM 在 `media_probe` 偶发 `MEDIA_AUDIO_PCM_FAIL`（视频仍成功），连续音频另案。
