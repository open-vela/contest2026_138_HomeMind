# 证据：家庭私有云本机 ASR 与唤醒后语音闭环（2026-09-15）

日期：2026-09-15。地点：家庭局域网私有云 192.168.31.251（Ubuntu 22.04，4 核 / 7.6GiB / 纯 CPU）。
关联：方案第 2 点「端云协同的语义理解与任务转化」、第 4 点「隐私与低延迟」，
以及「唤醒 → 说话 → 理解 → 执行 → 反馈」这条主动式交互主链。

## 1. 要解决的问题

09-15 之前，语义理解已经落在家庭本机（见 `2026-09-15_private_cloud_local_llm.md`），
但**语音入口仍然断在转写这一步**：

- 端侧 `voice` 走 `/v1/media/audio`，该端点由 `_deny_raw_media()` 保护。
- `PRIVACY_MODE=1`（默认）时它一律返回 **403**，原始音频不允许出站。
- 唯一的替代路径是把音频交给公网 MiMo 做转写 —— 与「音频不出家庭」直接冲突。

结果：能听懂话，但话要先经过第三方。这条主链在隐私模式下是断的。

## 2. 方案

把**转写**也搬到家庭本机，端侧只负责采集与上报：

```
ESP32-S3-EYE 麦克风
  → 录音（16k / 16bit / 单声道 PCM）
  → POST https://192.168.31.251/v1/voice/utterance   （局域网内）
      → local_asr.transcribe()      本机 faster-whisper 转写
      → agent_plan.plan_text()      本机 LLM 语义规划（三级回落 + 安全护栏）
      → agent_plan.execute_plan(announce=False)
  → 端侧取响应里的 plan.speak
  → POST /v1/media/announce → MQTT → 网关 → HA notify → 小爱音箱播报
```

全程音频与转写结果**不出局域网**，也不经过任何第三方。

关键实现：

| 组件 | 说明 |
| --- | --- |
| 推理引擎 | `faster-whisper`（CTranslate2 后端），CPU + int8 量化 |
| 模型 | `whisper-small`，约 460 MB，来自 ModelScope，落 `/home/hfy/homemind-models/` |
| 离线保证 | `HF_HUB_OFFLINE=1` + `local_files_only=True`，绝不联网取模型 |
| 误触发抑制 | RMS 门控（`ASR_MIN_RMS=180`）+ 幻觉正则 `_HALLUCINATION` + VAD 过滤 |
| 端侧入口 | `POST /v1/voice/utterance?rate=&device_id=&execute=&announce=` |
| 能力自检 | `GET /v1/voice/health`（无需鉴权，便于运维与端侧判断） |

### 2.1 为什么需要局域网 TLS 代理

端侧 `vela_https_request()` 是 **TLS-only** 实现（MBEDTLS_SSL_VERIFY_OPTIONAL），
无法直接请求明文 `http://192.168.31.251:8001`。因此在家庭主机上跑一个极小的
TLS 反向代理（`transfer/tls_proxy.py` + `homemind-tlsproxy.service`）：

- 自签证书，**只监听局域网地址** `192.168.31.251:443`，转发到 `127.0.0.1:8001`
- 不开放任何公网入站；API 本身仍只绑 `127.0.0.1`
- 端侧只信任该地址，凭据（`MEDIA_TOKEN`）随请求头发送

## 3. 实测结果（家庭私有云本机）

样本为现场的唤醒词录音，经 `/v1/media/audio`（隐私模式 → 本机 ASR）：

| 样本 | 类型 | 结果 | 耗时 |
| --- | --- | --- | --- |
| pos01 | 唤醒词 | `你好,OpenVilla` | 2.0s |
| pos03 | 唤醒词 | `你好,OpenVlan` | 2.0s |
| pos05 | 唤醒词 | `你好,OpenV来。` | 2.1s |
| pos07 | 唤醒词 | `你好,OpenVlan` | 2.0s |
| neg01 | 静音/噪声 | `text=""`，`reason=asr_empty` | **0.0s** |
| neg03 | 其它语音 | `哼!`（无动作） | 2.0s |

静音样本 **0.0s** 返回说明 RMS 门控在进模型前就拦掉了，没有白跑一次推理。

全链路 `/v1/voice/utterance`：

| 输入 | 结果 | 耗时 |
| --- | --- | --- |
| `你好,OpenVilla` | `source=local_llm`、`actions=0`、`executed=false` | 3.4s（含 ASR 2.0s） |
| `你好,OpenVlan` | `source=local_llm`、`actions=0`、`executed=false` | 3.4s（含 ASR 2.0s） |

问候语不产生任何动作 —— **不误触发控制**是这条链路的硬要求。

### 3.1 隐私门控未被削弱

`/v1/voice/health` 在隐私模式下：

```json
{"local_asr": {"enabled": true, "model_exists": true, "loaded": true,
               "egress": "none"},
 "privacy_mode": true,
 "note": "音频仅在本机转写，不出局域网"}
```

`/v1/media/audio` 的语义改为：**隐私模式 → 本机 ASR；本机 ASR 不可用 → 仍然 403**。
即「不降级外发」这条底线保留，只是把「能用的本机能力」补上了。

## 4. 遇到的问题与修复

1. **pip 下载被限速到 ~100 KB/s**，faster-whisper 依赖装不完。
   改为用 `curl` 直接抓 wheel 到 `/tmp/wheels`（约 1.5 MB/s），再
   `pip install --no-index --find-links` 离线装。
   抓取脚本还需过滤**架构与预发布版本**：armv7l/aarch64 的 wheel 会被误选，
   `rc`/`alpha` 预发布版本也会被误装。
2. **1.5B 模型照抄提示词里的占位文字**：实测返回
   `{"type":"xiaoai_execute","text":"对小爱音箱说的中文指令"}`。
   类型合法、内容却是模板串，一旦执行就会把示例文字真的发给小爱。
   修复：新增 `PLACEHOLDER_TEXTS` + `_is_placeholder()`，在 `sanitize_plan()` 里拦掉
   占位标题/占位文本/占位播报，并在系统提示里显式写明"不要照抄示例占位文字"。
3. **录音缓冲泄漏**：`pcm` 是 static 指针，原成功路径不释放，
   每轮语音泄漏约 96 KB（3s × 16k × 2B）。已在请求发完后立即 `free()`。
4. **板载灯被误判成家用灯**：`把开发板上的指示灯打开` 会走到 `xiaoai_execute`。
   已在 `local_rules()` 里把板载指示灯规则提到家用灯之前，并在提示词里加对照示例。

## 5. 端侧固件同步改造

`firmware/ai_agent_overlay/src/channels/nsh_commands.c`：

- 抽出 `hm_voice_utterance_roundtrip()`，`voice` 命令与 KWS 唤醒共用同一份实现。
- 去掉对公网大模型（MiMo）的依赖：不再需要 `api_key` / `llm_host` 即可完成
  "说话 → 执行"，断网或隐私模式下语义链路仍可用。
- KWS 持续监听新增 `kws_listen voice on|off`（默认 **on**）：唤醒后让出麦克风 →
  录音 → 云端闭环 → 重开麦克风继续监听；重开失败则置 `run=0` 走正常清理路径退出。
- **不再把原文二次送入端侧 agent 管线**：语义规划与执行已在私有云完成，
  否则"开灯"会被执行两次。
- 播报走 `announce=0`（云端不播）+ 端侧自己调 `/v1/media/announce`，
  避免同一条回复被播两遍。

## 6. 板端验收（2026-09-15 14:20–14:35）

本轮把「板子麦克风只出 640 字节 / KWS `frames=0`」这个卡了一天的现象查清了，
**根因不是"需要拔插 USB 真正下电"，而是麦克风被两个消费者同时打开**。

### 6.1 根因：`/dev/audio/pcm_in0` 必须单消费者

`nuttx/audio/audio.c` 的 head/tail 记账在**重新 open / 并发 open** 后会错位，
且音频缓冲来自一个固定大小的池。一旦两个持有者同时 open，就会：

- `poll()` 立刻返回 `POLLIN|POLLERR` 而 `apb->nbytes` 仍为 0 → 只拿到第 1 块 640 字节；
- 缓冲池被耗光后，下一个 `AUDIOIOC_ALLOCBUFFER` **永久阻塞** → 整机控制台无响应。

实测复现（同一固件、同一上电周期）：

| 场景 | 结果 |
| --- | --- |
| 干净上电后**单消费者** `audio_stream 2` | `DONE bytes=64000/64000 peak=16384` → **PASS continuous** |
| KWS 在听时再跑 `audio_stream` | `chunk=1/2` 正常，`chunk=3 nbytes=0`，随后整机无响应 |
| 长 KWS 会话结束后从 CLI 跑 `voice` | 卡在录音内不再返回（缓冲池耗尽） |

结论：**麦克风是独占资源**。KWS 在听时不要再跑 `voice` / `audio_stream`；
诊断必须"先停 KWS、再单测"，复位才能解除已耗尽的状态。

原先 09-13/09-14 观察到的 `frames=0`，是再武装脚本自己在 KWS 起来之后又碰了一次
音频设备导致的 —— 与被测固件无关。

### 6.2 唤醒 → 录音 → 私有云转写 → 回到监听（已跑通）

单消费者重跑后，真实语音链路的每一段都有日志：

```
[KWS] f2=0.082 f3=0.519 f4=0.087 z=8.718
[KWS-L] score=1.000 thr=0.55 frames=60      ← 唤醒词命中（阈值 0.55）
[KWS-L] HIT #1
[Voice]: recording ~3s, speak now...
[Audio] chunk=150 nbytes=640 total=96000     ← 连续采集，无掉块
[Voice-DBG] captured 96000 bytes
[Voice]: captured 96000 bytes, transcribing...
[HM-NET] connected ipv4=192.168.31.251 fd=4
[HM-TLS] Handshake OK: TLSv1.2, TLS-ECDHE-RSA-WITH-AES-256-GCM-SHA384 (310ms)
[HM-NET] HTTP write start: POST /v1/voice/utterance?rate=16000&device_id=esp32s3-eye&announce=0
[HM-NET] HTTP status: 200
[Voice]: no speech recognised (asr_empty)
…随后 KWS 重新开麦继续监听（frames 继续增长，无死机）
```

服务端同一时刻的对应记录：

```
INFO:faster_whisper:Processing audio with duration 00:03.000
INFO:faster_whisper:VAD filter removed 00:03.000 of audio
INFO:local_asr:local asr: no speech / hallucination filtered:
INFO:     127.0.0.1:48204 - "POST /v1/voice/utterance?rate=16000&device_id=esp32s3-eye&announce=0 HTTP/1.1" 200 OK
```

即：**唤醒 → 端侧连续采集 → 局域网 TLS → 本机 ASR → 回到监听**整条链路成立，
中途不冻结；`asr_empty` 是静音样本的正确返回，不是故障。
音频只到 `192.168.31.251:443`，转写在本机完成，无任何第三方出站。

### 6.3 唤醒后的阻塞与"连读"问题（已修）

实测两次真实唤醒都是 `score=1.000` 但云端 `asr_empty`，定位到两个原因：

1. **唤醒后到开麦之间有约 0.6–0.8s 的阻塞**：原顺序先做
   `hm_led_blink(3, 80, 80)`（约 480ms）再发 MQTT 唤醒事件，然后才关麦、
   `usleep(150ms)`、开始录音。用户紧接着说的指令会被挤掉。
   改为：唤醒后只做一次 50ms 光脉冲，**MQTT 唤醒事件挪到录音与上报之后**。
2. **用户习惯把唤醒词和指令连成一整句说**。KWS 要"人声之后出现静音"才判定成功，
   等 HIT 触发时指令早就说完了 —— 这种情况下录音窗口再长也没用。

第 2 点用**听得见的应答**解决：新增 `POST /v1/voice/wake`，由家庭音箱回一句
"我在，请说"（后台播报、立即返回，失败不影响录音），端侧等它播完再开麦。

```
[KWS-L] HIT #1
[Voice] wake prompt -> 200      ← 请小爱回一句"我在，请说"
（等 2.2s，用户听到后开口）
[Voice]: recording ~5s, speak now...
```

录音窗口**最终保持 3s**（`HM_VOICE_SECONDS`）。中间曾试过放宽到 5s，
但真机上出现「`/v1/voice/wake` 通了、随后的 `/v1/voice/utterance` 却没出现」：

- 采集循环 `hm_audio_stream_session()` 里有写死的 `if (chunk > 200) break;`，
  每块 640B → **128000B**，正好是 16kHz/16bit 单声道下的 **4 秒**；
  3 秒（96000B）一直在安全区内，所以这条上限从未暴露。
- 对照实验（干净单消费者）：`audio_stream 2`（64000B）→ `PASS continuous`；
  `audio_stream 5`（160000B）→ **完全没有输出**。
- 进一步复测显示 160000B 这个量级会**把音频缓冲池耗光、整机失去响应**
  （RTS 软复位救不回来，需重新用 esptool 握手才恢复），所以真实瓶颈是
  **内存余量**，不是循环计数器。

处置：录音窗口退回 3s；把写死上限改成按尺寸推导
`if (chunk > (want / bsize) + 40) break;` 以免"改时长就静默失败"；
并把硬编码的 `"recording ~3s"` 改成打印真实秒数。
**注意：不要据此把录音时长调到 4 秒以上。**

> 备注：`POST /v1/voice/wake` 走 `app/xiaoai.py` 直连 Home Assistant，
> **不经过 MQTT 网关**，因此网关停着也能喊人 —— 调测时很有用。

### 6.4 端侧循环漏内存的坑

`hm_voice_utterance_roundtrip()` 里的 `pcm` 与 `resp` 是 `static`，
调用它的 KWS 线程必须给足栈：默认 pthread 栈放不下 mbedTLS 握手 + 4KB 响应缓冲，
表现为"唤醒一次后整机静默、云端收不到请求"。监听线程现用 `pthread_attr_setstacksize`
显式给 32KB。

## 7. 局限（如实列出）

- **"用户说指令 → 被转写并执行"这一腿尚未取到成功日志**。已确认的是：
  唤醒、采集、TLS、本机 ASR、回到监听全部正常，且静音样本被正确拒绝；
  失败样本都是"指令在唤醒前已说完"，对应的"我在，请说"应答与 5s 窗口
  已在代码中就位，但**重启后端与重刷固件后的复测还没做**，因此不主张已完成验收。
- 唤醒后先播提示音再开麦，端到端多约 2.5s（提示音本身 + 等待），
  首次交互延迟实测约 6–8s（含 ASR 约 2s 与本地 LLM 规划 3–5s）。
- 录音窗口固定 **3s**（`HM_VOICE_SECONDS`），长指令仍会截断；尚未做端点检测自动收尾。实测超过约 4 秒（128000B）会耗尽音频缓冲池并锁住整机，见 6.3。
- **不主张关键词唤醒准确率达标**。`kws_listen` 的阈值（`thr=0.55` / `vad=900`）
  未在足够样本上做过准确率统计，验收时只按"持续监听可跑通"表述。
- 本机 ASR 是 CPU int8 推理，约 2s/句；叠加 3–5s 的本地 LLM 规划，
  端到端 3–5s，**不是毫秒级**。
- 局域网 TLS 代理目前用自签证书，未接入局域网 CA。
- 仍未做：人脸/手势视觉、小程序真机长稳。
