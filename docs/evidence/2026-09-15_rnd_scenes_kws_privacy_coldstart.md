# 2026-09-15 感知事件 / 睡前场景 / 提醒 / 媒体出站 / 持续 KWS / 冷启动

固件候选构建日志：`/tmp/homemind_build_rnd_20260915.log`（板端验收后回填 BIN 哈希）。

## 1. 家庭 API 端到端（本机 127.0.0.1:8001）

命令：`/home/hfy/e2e_rnd_accept.py`，结果 **PASS 11 / FAIL 0**（2026-09-14 13:52 UTC 左右）。

| 项 | 结果 |
| --- | --- |
| `GET /v1/scenes/list` | 睡前场景可列出 |
| `POST /v1/scenes` dry_run | intent 落库，actions=dry_run，≥2 条待办 |
| `POST /v1/scenes` real | `led.off` queued；米家动作走白名单 |
| `POST /v1/media/frame` / `audio`（无 token） | 503 fail-closed |
| `POST /v1/media/frame` / `audio`（有 token + PRIVACY_MODE=1） | **403 privacy mode: raw audio/video outbound denied** |
| MQTT `homemind` person_detected | `HomeEvent` 落库 event_type=person_detected |
| 任务 remind_at 到点 | note 标记 `#reminded`，`task_reminder` 事件落库 |
| `GET /v1/events` | 可查询 reminder/scene/person 事件 |

新增源码：
- `backend/api/app/routers/scenes.py` — 睡前场景编排
- `backend/api/app/reminder_worker.py` — 到期提醒 worker
- `backend/api/app/perception.py` — 感知事件入库与触发待办
- `backend/api/app/mqtt_client.py` — 订阅 `device/+/event` 与 `homemind`
- `backend/api/app/routers/media.py` — `_deny_raw_media()` 严格出站门控
- `backend/api/app/main.py` — 注册 scenes + 启动 reminder worker

部署副本：`/home/hfy/homemind-backend`（systemd homemind-api 已重启并 active）。

## 2. 板端持续离线 KWS（代码已入 overlay）

`kws_listen start|stop|status [thr] [vad_thr]`

- 后台线程持续读 I2S PCM（poll 首块 + usleep 后续，单会话）
- 能量 VAD 门控 → 约 1.2s 语音窗 → `kws_score_pcm`
- HIT：LED + LCD + MQTT `{"type":"kws_wake",...}`（离线时仅本地）
- 非网络依赖；**关键词判别率仍受既有样本/域差异限制**，见
  `docs/evidence/2026-09-12_kws_honest_status.md`。本项交付的是**持续监听闭环**，不把静音全触发或说话不触发的历史模型写成已达标。

## 3. 冷启动断网

`agent_main.c` Phase 5 在 network_watch 等待 Wi-Fi **之前**启动 `agent_loop`：

- 本地工具（LED/device/vision/wake/kws_listen）与 ask 队列可立即处理
- LLM 调用离线快速失败，不再“联网后才启动导致 ask 无限排队”
- MQTT/微信/WS 仍等网络连上再 start

`tools/deploy-to-vm.sh` 已纳入补丁 `0004-homemind-esp32s3-i2s-audio-buffer-info.patch`。

## 4. 边界与未主张

- 人脸/手势身份识别：**未完成**（仍为人脸混合存在检测实验）
- KWS 指定词准确率：**未达标**，不写“已实现你好，openvela 唤醒”
- 小程序跨端日程真机同步：后端 API 已具备 tasks/remind，小程序本机存储仍需联调
- 板端 `kws_listen` / 冷启动 **以本轮构建烧录后串口验收为准**；文档不提前写成已验收


## 5. 固件构建与板端串口验收（2026-09-14 13:56 UTC）

| 项 | 值 |
| --- | --- |
| BIN 大小 | 1,421,972 bytes |
| BIN SHA-256 | `0108fa089533e2fc38d1bdda76710fa299f573a47b76245ded6996997dfa5b98` |
| BIN MD5 | `aecb019bbbc8f1aa381971cbbebcb689` |
| 烧录 | esptool 写入成功，Hash verified |
| `audio_stream 1` | PASS continuous 32000/32000 |
| `kws_listen start` | started + listening |
| `kws_listen` 8s | **frames=155**（持续采样闭环成立） |
| `kws_listen stop` | stopped frames=155 hits=0 |
| 关键词 HIT | 本轮无真人说唤醒词，**未宣称命中率** |

早期 `frames=0` 为观测窗口过短；加长到 8s 后正常累计。


## 6. 冷启动产物核对

- `artifacts/nuttx.elf` / `nuttx.bin` 含字符串：
  - `Cold-start offline agent_loop ready (local tools OK)`
  - `agent_loop_start(offline)`
  - `kws_listen start|stop|status ...`
- esptool hard-reset 后串口可见 NSH/CLI 线程在 Wi-Fi 扫描完成前已起来（`[CLI] thread stack=...`）。
- 启动阶段 P5 日志走 **syslog**，串口未必逐行打印；以 ELF/BIN 字符串 + 源码 Phase5 注入为准。
- 服务 homemind-gateway 已恢复 active。

## 7. 仍未完成

- 指定词 KWS 判别率（样本/域差异）未达标，不写「已实现你好，openvela 唤醒」
- 人脸/手势身份识别未完成
- 小程序跨端日程真机联调未做
- 性能/功耗/长稳（≥4h）验收未跑
