# 控制台"零字节"的真因 + 麦克风解码的多真值实验

日期：2026-09-16
设备：ESP32-S3-EYE（MAC `a4:cb:8f:e1:e0:08`），固件 `artifacts/nuttx.bin` md5 `a1a9941a5eace6acee1e147084bad8a0`
（读回比对确认 flash 内容与构建产物逐字节一致，前 128 KB md5 `092fe18702e4458b44546dc6181edce8`）

---

## 一、结论先说：板子从来没有"锁死"

前一天把"串口永远 0 字节、复位无效、重刷无效"判成了**芯片被闩住、只能物理拔插 USB**。
本轮在用户拔插（确实重新枚举：device 4 → 5，`ttyACM0` → `ttyACM1`）之后查清，**这个判断是错的**。

### 1.1 真因

ESP32-S3 的控制台是 **USB-Serial-JTAG** 外设，内部只有一个约 **845 字节**的 FIFO。
芯片在**没有宿主读取**的情况下走完启动流程时，NuttX 紧接着那一次控制台写
（`NuttShell (NSH)`）**永久阻塞**——之后无论谁再来读，都不再吐一个字。

证据：所有失败样本读到的字节数**精确地**都是 845，且内容**精确地**停在
`total segments stored 6` 这一行之后：

```
ESP-ROM:esp32s3-20210327
Build:Mar 27 2021
rst:0x15 (USB_UART_CHIP_RESET),boot:0x2a (SPI_FAST_FLASH_BOOT)
Saved PC:0x40041a76
SPIWP:0xee
mode:DIO, clock div:2
load:0x3fcc6000,len:0x4420
load:0x40374000,len:0xbb68
SHA-256 comparison failed:
Calculated: 8ba1dcbe8e556597b88ab458a76cdbf7d1203d227467f1dbe72fc5c262a9915b
Expected: 0000000030000000000000000000000000000000000000000000000000000000
Attempting to boot anyway...
entry 0x40375118
*** Booting NuttX ***
dram: lma 0x00000020 vma 0x3fcc6000 len 0x4420   (17440)
iram: lma 0x00004448 vma 0x40374000 len 0xbb68   (47976)
padd: lma 0x0000ffc8 vma 0x00000000 len 0x30     (48)
imap: lma 0x00010000 vma 0x420a0000 len 0xce874  (845940)
padd: lma 0x0000de87c vma 0x00000000 len 0x177c  (6012)
dmap: lma 0x000e0000 vma 0x3c010000 len 0x7b528  (505128)
total segments stored 6          ← 到此 845 字节，FIFO 满，NuttX 卡在这里
```

一次**成功的**采集（同一颗芯片、同一份 flash）会在其后继续出现：

```
NuttShell (NSH)
nsh>
[HM-WIFI] ifup ret=0
[CLI] thread stack=0x3c2e7e30
vela>
[HM-WIFI] Selected strongest AP 50:88:11:7a:02:69 (-46 dBm)
[HM-WIFI] ifdown_post_scan ret=0
[HM-WIFI] ifup_post_scan ret=0
[HM-WIFI] mode ret=0
[HM-WIFI] wpa_version ret=0
[HM-WIFI] cipher ret=0
[HM-WIFI] psk ret=0
[HM-WIFI] essid_pre_ap try=1 ret=0
```

### 1.2 为什么当时的排查链会把人带向错误结论

| 检查 | 结果 | 当时得出 | 实际含义 |
|---|---|---|---|
| `lsusb` | `303a:1001` 在 | USB 层活着 | （正确） |
| `esptool -p … chip-id` | 连上、读到 MAC `a4:cb:8f:e1:e0:08` | 芯片活着 | **已经证明芯片活着** |
| `build.sh flash` | 写入 + `Hash of data verified`、rc=0 | flash 通路正常 | （正确） |
| `dmesg` 复位前后 | 无任何 USB 重枚举 | 复位没生效 → 芯片被闩住 | **USB-JTAG 是外设、不重枚举才是正常的**；据此推向"锁死"是错的 |
| 重刷固件 | 能写能校验 | 排除固件损坏 | （正确） |

**教训**：`esptool` 能连上就已经证明芯片是活的。"控制台没有输出"首先要怀疑
**读取方式**（谁在读、什么时候开始读、FIFO 有没有被顶满），不能直接推断成硬件锁死。
这一条把一整轮排查引到了错误方向，并让用户白做了一次拔插。

### 1.3 它是间歇性的

矩阵试验（每次都用 esptool 真复位，只改两个变量）：

| 变体 | 复位后到附着 | 首次敲回车 | 结果 |
|---|---|---|---|
| 1 | 1.0 s | t=3 s | **成功**（`NuttShell`/`vela>`/`[HM-WIFI]` 齐全） |
| 2 | 1.0 s | t=0 s | **成功** |
| 3 | 0.2 s | t=3 s | **成功** |
| 4 | 0.2 s | t=0 s | **成功** |

两个变量都不是原因；同一份 flash、同一条命令，实践中出现过"单次失败"与"4/4 全成功"两种结果。

### 1.4 对策（已固化到 `tr_plan.py` v4）

每次会话固定走「**复位 → 立刻附着 → 校验是否真进了 shell → 不成功就重试**」：

1. `esptool --chip esp32s3 -p /dev/ttyACM1 --after hard_reset read-flash 0x0 0x100 <tmp>`（复位）
2. **open 之前**置位 `dtr=False; rts=False` 再 `open()`（见 1.5）
3. 等 `nsh>`/`vela>`（原始流尾部正则，提示符后面没有换行）；见到 `nsh>` 自动送 `ai_agent`
4. `HM_TRIES`（默认 4）次重试，`HM_WAIT`（默认 30 s）

v4 首跑即成功，并当场跑完一次 `voice` 全链路。

### 1.5 顺带确认：DTR/RTS 会干扰

pyserial 打开端口默认 assert DTR/RTS，而 USB-JTAG 把它们桥到 EN/BOOT。
四种组合实测：`dtr=False` 才读得到输出，`dtr=True` 完全无输出。所以要在 **open 之前**置位。

---

## 二、多真值实验：麦克风解码仍未定案，但方向变了

### 2.1 方法（为了让整场只有一个进程攥着串口，改为绕过网关直接打 HA）

`POST {HOMEMIND_HA_URL}/api/services/notify/send_message`
body `{"entity_id": "notify.xiaomi_cn_2085562629_lx06_play_text_a_5_1", "message": text}`
（`HOMEMIND_HA_URL` / `HOMEMIND_HA_TOKEN` 在 `/home/hfy/homemind-backend/.env`）

`_xiaoai_decode_exp.py`：复位+附着 → 预热一次 `voice` → 每句"重复 4 遍"播报后连采 3 个 5 秒窗口。
真值：`打开客厅的灯。` / `关闭卧室的灯。` / `打开书房的空调。` / `播放一首音乐。`

### 2.2 采到了人声，且端侧真实完成了一次业务闭环

```
[Voice]: recognised at 16000 Hz
[Voice]: 但我还想说你空调但我还想说你空调          ← 真值「打开书房的空调」
[Voice]: resp={"asr":"local_asr","asr_ms":2252,"egress":"none",
               "plan":{"intent_type":"device.control",
                       "actions":[{"type":"xiaoai_execute","text":"打开空调"},
                                  {"type":"speak","text":"好的，空调已打开"}],
                       "source":"local_llm"}}
[Agent]: 好的，空调已打开
[Voice]: announce http=200（音箱应已播报）
```

稳态 rms 分布：本底 ≈5–8，小爱语音 ≈700–800（静音门 `ASR_MIN_RMS=180`）。

### 2.3 解码一致性判定（8 种候选 × 每句最响的 2 条）

| 解码 | 关闭卧室的灯 | 打开书房的空调 | 播放一首音乐 | 全中句数 |
|---|---|---|---|---|
| `avg2 @16000` | 2/3「关闭**我是个**灯」 | 1/3「**打开收藏的空桥**」 | 1/2「**不放一首音乐**」 | 0 |
| `avg2 @8000` | 0/3（空） | 1/3 | 1/2 | 0 |
| `avg4 @4000` | 0/3 | 1/3 | 1/2 | 0 |
| `raw @16000` | 0/3 | 1/3 | 0/2 | 0 |
| `pick2 @8000` / `pick2 @16000` / `raw @8000` / `avg4 @8000` | 0 | 0 | 0 | 0 |

（`打开客厅的灯` 那一句三次采集全是近静音，未计入。）

**没有任何变体做到 4 句全中，而且所有变体都糊。** 说明"换后处理"救不了。

### 2.4 关键对照：伪影是本轮采集路径特有的

| | >4 kHz / ≤2 kHz 能量比 | 奇偶位非零率 | `[x,0]` 结构 |
|---|---|---|---|
| Sep-12 `record/pcm/*.pcm`（同一硬件、另一条采集路径） | **0.02** | 都 91% | **无** |
| 本轮设备流 | **0.43–1.49** | — | **有**（安静段也严格如此） |

⇒ 本轮 I2S 路径确实引入了与信号无关的刚性结构（每个真实样点被复制，并伴随零值插入，
表现为 >4 kHz 能量是 <4 kHz 内容的镜像），而**另一条路径没有**。
所以问题在**采集端**，不在云端/ASR/后处理。

### 2.5 一个容易踩的坑

小爱把同一句重复 4 遍时，5 秒录音窗口里天然含 2–3 遍 ⇒ 转写文本里的"双份"
（`但我还想说你空调但我还想说你空调`）是**语音本身重复**，不能当成"每个采样被读了两遍"的伪影证据。

---

## 三、新发现的缺陷：`bad audio size`（400）

端侧采集块数会浮动，上传长度因此是 151680 / 152320 / 160000 字节：

```
[STRM] out=151680 bytes (75840 samples) chunks=237 zero=0 dup=16 …
[Voice-ERR]: utterance rate=16000 http=400 body={"detail":"bad audio size"}
[Voice-ERR]: utterance rate=8000  http=400 body={"detail":"bad audio size"}
[Voice]: no speech recognised (asr_empty)
```

云端 `routers/voice.py`：

```python
raw = await request.body()
if not raw or len(raw) > MAX_AUDIO_BYTES:
    raise HTTPException(status_code=400, detail="bad audio size")
```

即"不足整段"也会被拒（实测）。⇒ 修法应在**端侧**：采集不足预算长度时补齐再上传；
或云端放宽为"最短 N 秒"判定。否则约 1/3 的采集会静默变成 `asr_empty`。

---

## 四、复现方式

```bash
# 1) 停网关，让出串口（整场只让一个进程攥着端口）
echo 123456 | sudo -S systemctl stop homemind-gateway

# 2) 定案采集（复位+重试附着+小爱当音源）
python3 /tmp/_xiaoai_decode_exp.py

# 3) 离线一致性判定
MAX_PER_TRUTH=2 /home/hfy/homemind-backend/venv/bin/python /tmp/_decode_verdict.py

# 4) 恢复
echo 123456 | sudo -S systemctl start homemind-gateway
```

---

## 五、下一步（建议）

1. **从驱动侧把采样率定下来**，不要再靠后处理推断：在设备侧打印
   `I2S_RX_TDM_TOT_CHAN` / `I2S_RX_TDM_WS_WIDTH` / data width / BCLK 分频 / RX CONF 寄存器原文，
   把"每帧几个 slot、DMA 字宽"从推断变成实测。
2. 按实测结果修 RX 通道配置，使流成为**干净的 16 kHz 单声道**。
3. 修 `bad audio size`（端侧补齐上传长度）。
4. 收尾时清掉 live API `routers/voice.py` 里的 `/tmp/voice_dump` 调试转储钩子。
