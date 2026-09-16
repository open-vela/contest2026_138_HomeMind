# 麦克风"只能采到 0 与 -16384"根因定位与修复（2026-09-16）

## 结论

ESP32-S3-EYE 的数字 MEMS 麦克风此前只能采到恒定样点（`0` 与 `-16384`），
**根因不在麦克风硬件，而在 NuttX I2S 驱动把 RX 的 WS 宽度写反。**

`nuttx/arch/xtensa/src/esp32s3/esp32s3_i2s.c` 的 `i2s_set_datawidth()` 里，
TX 分支按 Philips I2S 规范写 `I2S_TX_TDM_WS_WIDTH = data_width - 1`
（WS 高电平 = 半个帧 = `data_width` 个 BCLK），而 RX 分支写的是 `1`
（WS 高电平只有 2 个 BCLK）。WS 宽度不对 ⇒ 数字麦无法帧同步 ⇒ SDOUT 停在空闲电平
⇒ 采集到的永远是同两个码字。修复即让 RX 分支与 TX 一致：Philips 模式下
`data_width - 1`，只有 PCM 模式才用 `1`。

修复后已固化为 `firmware/patches/0005-homemind-esp32s3-i2s-rx-ws-width.patch`。

## 证据链（逐步排除）

1. **拿到真机原始 PCM**：给 live API 的 `POST /v1/voice/utterance` 加临时转储钩子，
   把上传的 PCM 落到 `/tmp/voice_dump/`，再用 SFTP 取回统计（不必重新刷写即可观测）。
   修复前结果：80000 样本里只有 `0`（74.91%）与 `-16384`（25.09%），**没有第三个值**，
   且非零游程长度恒为 1 —— 恒定方波，典型的"数据线停在空闲电平"。
2. **排除 slot 位宽 / BCLK 比例**：改成运行时可调 `I2S_RXDATAWIDTH`（8/16/24/32 位），
   四组数据**完全一致** ⇒ 不是位宽错位，也不是 BCLK 分频问题。
3. **排除引脚**：SD=GPIO2 / SCK=GPIO41 / WS=GPIO42，与 EYE 原理图及 `scripts/build.sh`
   一致；`boards/.../esp32s3-eye/src/esp32s3_bringup.c` 写明 "The EYE microphone is an
   I2S RX-only digital MEMS device"，板上**没有 codec** ⇒ 不存在"codec 未初始化"这条路径。
4. **读驱动实现** → 发现 RX/TX 的 WS 宽度写反。修好并刷写后麦克风立即出真波形。

## 一个被实测推翻的中间结论（留档）

看代码时发现 `i2s_rxchannels()` **只保存 `priv->channels`、从不写
`I2S_RX_TDM_TOT_CHAN`**，据此推断"RX 恒定每帧产出 2 个 slot、麦克风在左 slot、
右 slot 恒为数字静音"，于是先实现了"逐块抽取左通道（去交错）"。

**实测把它推翻了**：会话结束新增的 `[STRM]` 计数器给出
`nzL=6841 nzR=6380`、`nzL=9429 nzR=11757` —— 右 slot 的非零计数与左 slot **相当**，
右 slot 并不静音；同时 `zero=0 dup=0`（驱动既不欠载也不重复投递）。
去交错把真实样点砍掉一半，两个用例的判定从 `PASS continuous` 退化成 `PARTIAL`。

**改回整块拷贝后**：

```
[STRM] out=53120 bytes (26560 samples) chunks=83  zero=0 dup=0 ms=2010 steady_ms=1990 eff_rate=13185 Hz nzE=6526  nzO=6708
[Audio] DONE bytes=53120/64000 peak=32768 rms=1970.6  → PASS continuous
[STRM] out=93440 bytes (46720 samples) chunks=146 zero=0 dup=0 ms=3000 steady_ms=2980 eff_rate=15570 Hz nzE=9976 nzO=10762
[Audio] DONE bytes=93440/96000 peak=43  rms=5.4      → PASS continuous
```

**故 RX 输出就是 16 kHz 单声道流，不需要去交错。** 教训：驱动里"没写某个寄存器"
不等于"硬件按默认值产生了你猜的那种帧结构"——必须用计数器去证伪。

## 采样率定论：16000 Hz

5 秒录音窗口下：

```
[STRM] out=160000 bytes (80000 samples) chunks=250 zero=0 dup=0 ms=5000 steady_ms=4980 eff_rate=16000 Hz nzE=18817 nzO=17767
```

250 块 / 4980 ms = 19.92 ms/块；每块 640 B = 320 样点 ⇒ **16063 样点/秒**。
即采集循环本身就是被硬件节流的，麦克风真实速率 = **16000 Hz 单声道**，
与 `CONFIG_ESP32S3_I2S0_SAMPLE_RATE=16000` 一致。

短采集（2 秒）会被开机预热压低到 13185 Hz —— 同样的硬件、同样的循环，只因为
固定开销占比不同。因此 `[STRM]` 的速率改成**从"第一块真正拿到数据"之后起算**的
稳态值（`steady_ms`），并额外打印整段墙钟 `ms` 供对照。

## 本轮落地的改动

全部在 `firmware/ai_agent_overlay/src/channels/nsh_commands.c`：

1. **采集时长按墙钟预算**：`budget_ms = want * 1000 / (rate * 2)`。原实现以"读到的
   块数"为退出条件（历史上写死 `chunk > 200`，后改按 `want/bsize` 推导），两种写法
   都会让"改录音时长"变成**静默失败**。
2. **`[STRM]` 取证行**：`out / samples / chunks / zero / dup / ms / steady_ms /
   eff_rate / nzE / nzO / budget`。`zero` 多 ⇒ 驱动欠载，`dup` 多 ⇒ 驱动重复投递（过载），
   `nzE/nzO` 用于判定是否存在交错结构。
3. **上报速率取实测稳态值并吸附到标准档位**，云端
   `/v1/voice/utterance?rate=` 只接受 8000..48000。
4. **采样率二次尝试**：实测档位本身是带抖动的估计，若云端回 `asr_empty`，
   用另一档（实测是 16000 就用 8000，否则用 16000）**重发同一段 PCM**，
   不必重新录音。

## 过程中踩到的部署坑（已修）

`tools/deploy-to-vm.sh` 原本**按文件名显式应用**补丁：0001/0002/0003 有调用，
而 **0004 只定义了变量、从未被调用** —— "补丁目录里有文件"并不代表会被打上。

为登记 `0005` 而引入宽松的 `apply_optional_nuttx_patch` 后反而出事故：
live 树是手工改过的，`patch --forward` 的 dry-run 仍然成功，于是同一个 hunk 被
**重复插入**；又因为 `scripts/build.sh build` 内部也会调 `deploy`，一次构建实际打了
**两遍** —— `esp32s3_i2s.c` 累积出 **3 份** `case AUDIOIOC_GETBUFFERINFO`，
编译报 `duplicate case value`。

现改为标记幂等的 `ensure_marker_patch()`：先 grep 标记，在就跳过，不在才打，
打完再验标记；并额外断言 `case AUDIOIOC_GETBUFFERINFO` 恰好 1 个。

## 复现与判据

```bash
OPENVELA_ROOT=$HOME/work/openvela-clean-20260830 JOBS=2 ./scripts/build.sh deploy
OPENVELA_ROOT=$HOME/work/openvela-clean-20260830 JOBS=2 ./scripts/build.sh build
ESPTOOL_PYTHON=$(command -v python3) SERIAL_PORT=/dev/homemind-esp32 \
  OPENVELA_ROOT=$HOME/work/openvela-clean-20260830 JOBS=2 ./scripts/build.sh flash
# 串口（必须先 stop KWS 保证麦克风单消费者）
kws_listen stop
audio_stream 3
```

判据：

- `eff_rate ≈ 16000 Hz`（稳态），`zero=0`、`dup=0`；
- `[Audio] DONE ... PASS continuous`；
- `nzE ≈ nzO` ⇒ 无交错结构，不要做去交错；
- 端到端：`POST /v1/voice/utterance` 返回 200，且云端日志**不再是**
  `local asr: silence (rms<180), skipped`。

## 端到端现状（2026-09-16 收尾时）

链路已全线打通并留证：

- 端侧 5 秒录音 → `out=160000 bytes`、`eff_rate=16000 Hz`；
- TLS 上传成功，云端日志出现
  `POST /v1/voice/utterance?rate=16000&device_id=esp32s3-eye&announce=0 HTTP/1.1" 200 OK`
  （两次尝试 `rate=16000` / `rate=8000` 均 200）；
- 抓回的 PCM 经统计确认是**纯房间本底**（`rms=6.76`、`max=43`），
  被 `ASR_MIN_RMS=180` 静音门正确拦下 ⇒ 这一段没有真实语音。

**唯一缺的就是一段真人语音**：需要在 `voice` 的 5 秒录音窗口内靠近设备说话。
本轮两次窗口（小爱播报后约 30 秒、60 秒）内均无人声。

### 残留观察项

- **首个 DMA 缓冲有启动毛刺**：某次 5 秒录音首块出现 `min=-7680`（其后本底仅 ±6），
  另一次则没有。对 ASR 无害（能量占比小、VAD 会滤掉），但值得后续在采集起点丢弃 1 块。
- **本机麦克风电平偏低**：静音本底 `rms ≈ 5–6`（约 -75 dBFS）。
  正常对话（1 m，60 dB SPL）对应 `rms ≈ 650`，仍高于 180 的门限；
  但若在 1 m 外轻声说话，可能落在门限之下。必要时先加数字增益再降门限。
- **刚重启后的首个 HTTPS 请求会 `http=-1`** 一次（TLS 层失败，云端无请求记录），
  之后正常。端到端测试已在正式录音前加一次 `voice` 预热来规避。
