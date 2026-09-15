# KWS 冷启动 frames=0 与静音误唤醒的根因与修复（2026-09-15）

## 0. 一句话结论

`kws_listen` 冷启动后一直 `frames=0`（麦克风打开成功却一包音频都读不到），
不是板子坏了、也不是代码回归，而是**两处独立的缺陷**：

1. `kws_listen_open_mic()` 打开麦克风后**没有调 `AUDIOIOC_GETBUFFERINFO`**，
   导致上层 `audio.c` 的 `nbuffers` 恒为 0，`ALLOCBUFFER` 每次都静默失败；
2. `hm_chunk_rms()` 的累加器用 32 位 `long`，**必然溢出**，
   使 VAD 能量门限彻底失效 —— 既会在室内静音时误唤醒，也会在调高阈值后永不触发。

修完这两处后 KWS 正常出帧，静音 138 秒 0 次误唤醒。

## 1. 现象与判据

```
$ kws_listen start
[KWS-L] started thr=0.55 vad=900
[KWS-L] listening thr=0.55 vad=900     ← 麦克风 open + CONFIGURE 都成功
$ kws_listen status
[KWS-L] run=1 hits=0 frames=0 ...      ← 50 秒后仍然 0
```

`frames` 只在 `kws_listen_read_chunk()` 返回 `n > 0` 之后才自增，
所以 `frames=0` 的含义是"**一次成功的读都没有**"。

## 2. 对照实验：先把范围锁死

同一块板、同一时刻（网关停掉以独占串口）：

| 命令 | 结果 |
|---|---|
| `audio_stream 5` | `DONE bytes=160000/160000 peak=16384 rms=7147.0` → **PASS continuous** |
| `kws_listen start` | `run=1 hits=0 frames=0`（50 秒） |

两条路径**用的是同一个麦克风 `/dev/audio/pcm_in0`、同一套 CONFIGURE**，
一条通、一条不通 ⇒ I2S / ES7210 / DMA 全部无罪，问题必然在 KWS 自己的读路径里。

## 3. 真因一：缺 `AUDIOIOC_GETBUFFERINFO`

`kws_listen_read_chunk()` 的四个失败分支全是静默 `return -1`，
先补上分类计数器，一次就打出决定性的数字：

```
[KWS-DBG] allocbuf fail errno=0
[KWS-DBG] calls=2793 alloc=2793 enq=0 start=0 empty=0
```

**allocbuf 失败率 100%，而且 errno = 0。** 去读 `nuttx/audio/audio.c`：

```c
static int audio_allocbuffer(FAR struct audio_upperhalf_s *upper, ...)
{
  if (upper->periods >= upper->nbuffers)
    {
      return 0;          /* ← 返回 0，既不是 sizeof(desc)，也不设 errno */
    }
```

而 `upper->nbuffers` **只有**一个地方会被赋值：

```c
      case AUDIOIOC_GETBUFFERINFO:
        {
          ret = lower->ops->ioctl(lower, AUDIOIOC_GETBUFFERINFO, arg);
          if (ret >= 0)
            {
              upper->nbuffers =
                  ((FAR struct ap_buffer_info_s *)arg)->nbuffers;   /* audio.c:1274 */
            }
        }
```

`kws_listen_open_mic()` 从来没调过这个 ioctl ⇒ `nbuffers = 0`
⇒ 每次 alloc 都走 `0 >= 0` 提前 `return 0`
⇒ KWS 的判定 `ioctl(...) != (int)sizeof(desc)` 不成立、而 errno 保持 0
⇒ `read_chunk` 恒返回 -1 ⇒ worker 里 `if (n <= 0) continue;` 空转。

`hm_audio_stream_session()` **一直有**这一句：

```c
    (void)ioctl(fd, AUDIOIOC_GETBUFFERINFO, (uintptr_t)&info);
```

这就解释了第 2 节的对照实验。也顺带解释了历史假象：
**以前"KWS 能用"只是因为同一次启动里先跑过 `voice` / `audio_stream`，
把 `nbuffers` 顺手设上了**；一旦冷启动直接起 KWS 就必然 0。
所以这不是回归，是一直存在的隐藏缺陷。

旁证：`firmware/patches/0004-homemind-esp32s3-i2s-audio-buffer-info.patch`
正是为了让 `GETBUFFERINFO` 能返回 `nbuffers = CONFIG_ESP32S3_I2S_MAXINFLIGHT`
而加的 —— 说明这条路径本来就依赖"先查 buffer info"。

### 修复

在 `kws_listen_open_mic()` 的 `CONFIGURE` 之后补上：

```c
    memset(&info, 0, sizeof(info));
    if (ioctl(fd, AUDIOIOC_GETBUFFERINFO, (uintptr_t)&info) >= 0) {
        printf("[KWS-L] bufinfo nbuffers=%u size=%u\n",
               (unsigned)info.nbuffers, (unsigned)info.buffer_size);
    }
```

### 验证

```
[KWS-L] bufinfo nbuffers=4 size=4095
[KWS-L] run=1 hits=0 frames=2000
[KWS-DBG] calls=2001 alloc=0 enq=0 start=0 empty=0
...
[KWS-L] stopped frames=3643 hits=0 last=0.000
[KWS-DBG] calls=3643 alloc=0 enq=0 start=0 empty=0
```

`frames` 稳定增长，3643 帧 / 74 秒 ≈ **20.4ms/帧**，与 640B@16kHz/16bit
单声道（=20ms 音频）对齐；四类失败计数全为 0。

## 4. 真因二：`hm_chunk_rms` 累加器溢出导致 VAD 门限失效

修好真因一之后暴露出第二个问题：**KWS 开始误唤醒**。
一次 60 秒的静音观察里出现 2 次 `HIT`（室内无人说话），
而两次上传录音都被云端判为 `VAD filter removed 00:05.000 of audio` / `asr_empty`
—— 说明命中的窗口里根本没有语音。

```c
static float hm_chunk_rms(const unsigned char *buf, int nbytes)
{
    long sum = 0;                     /* ← xtensa 上 long 是 32 位 */
    ...
    for (i = 0; i + 1 < nbytes; i += 2) {
        int s = (int)(int16_t)(buf[i] | (buf[i + 1] << 8));
        sum += (long)s * s;           /* 单样本平方最大 1.07e9 */
    }
    return (float)sqrt((double)sum / (double)n);
}
```

一个 chunk 是 640 字节 = **320 个样本**，即使按本机实测的峰值 16384 估算：
`16384² × 320 ≈ 8.6e10`，而 32 位 `long` 上限 2.1e9、按 32767 估算更达 3.4e11
⇒ **必然回绕**。回绕值还可能为负，`sqrt(负数)` 直接得到 `-nan`。

后果是 VAD 门限形同虚设：

- 门限 900 时，回绕后的随机值偶然低于门限，`silence` 得以累积 →
  在**静音**上触发评分并误命中；
- 把门限提高到 3000 / 6000 后，60 秒内**一次评分都不触发**（门限永远不满足）；
- 实测本底：用同一个 `hm_chunk_rms` 的 `wake_loop` 量 30 秒，
  chunk RMS 的 min/p25/中位数/p75/max = **0 / 4579 / 8192 / 9606 / 11549**。

同一个坑在 `cmd_audio_stream()` 里已经用 `long long sum_sq` 修过一次
（该函数有注释记录），但**被 VAD / `wake_loop` 共用的这个版本漏掉了**。

### 修复

```c
    long long sum = 0;
    ...
        sum += (long long)s * (long long)s;
```

### 验证

`vad=900` 静音观察 138 秒：

- `HIT` 数 **0**（修前 60 秒 2 次）
- 评分窗口 **1** 个，且 `pk=16384 f2=0.744` → `score=0.000`
- `frames=5996`、四类失败计数全 0

## 5. 附带改进：把静默失败变成可观测

`kws_listen_read_chunk()` 原来四个失败分支都是裸 `return -1`，
worker 里 `if (n <= 0) continue;` —— 这是本次"找不到原因"的直接原因。
现已按 alloc / enqueue / start / 空包 四类分别计数，前 3 次打印，
并在 `kws_listen status` / `stopped` 行后一并输出。
失败路径补 `usleep(10000)` 避免 `continue` 退化成热转圈。
非首包等待从 30ms 改为 10ms，与采集端实时对齐（同 `hm_audio_stream_session` 的既有修复）。

## 6. 遗留问题（下一轮）

### 6.1 麦克风采集疑似整体饱和（**优先级最高**）

把 `wake_loop` 打出的每一个 chunk RMS 做拟合，全部精确满足：

```
rms² = 838860 × nz        （838860 = 16384² / 320，nz 为整数）
   1295.3² → nz=2        8192.0² → nz=80
   1831.8² → nz=4       11028.8² → nz=145
   3663.6² → nz=16      11549.0² → nz=164
```

即**每一个 20ms chunk 里只存在 |16384| 和 0 两种样点**
—— 这是硬削顶方波 / 类 1-bit 流，不是语音波形。

这与 `/v1/voice/utterance` 一直返回
`VAD filter removed 00:05.000 of audio` / `asr_empty` 完全一致：
上传的录音里从来没有可转写的语音内容。
也意味着**至今没有任何一次真实语音被成功转写**。

需要下一步确认方向：

- ES7210 增益寄存器是否被设到饱和（正常房间不可能持续削顶）；
- I2S 数据位宽 / slot 对齐（`CONFIG_ESP32S3_I2S0_DATA_BIT_WIDTH`）
  与 ES7210 实际输出格式是否匹配；
- 或 ES7210 未真正完成初始化，SDOUT 输出的是空闲位型。
- 需要新增一个"原始样点 dump"命令（打印前若干样点的整数值、DC 均值、
  单 chunk 内不同幅度的直方图）来区分上面三种情况。

### 6.2 重新标定 `vad`

默认 `vad=900` 远低于实测环境本底（中位数 8192）。
在真实语音电平确定之前，`vad` 的取值没有意义；
若 6.1 的饱和问题解决、电平恢复正常，需要重新标定。

### 6.3 调试打印降噪

`[KWS] pk=.. f2=.. f3=.. f4=.. z=..` 每个评分窗口都会打印，
演示场景下偏吵，可在标定完成后降级为按需开启。

## 7. 相关提交

- `62eee01` fix(kws): 修掉麦克风缓冲池未初始化与 VAD RMS 溢出，KWS 从 frames=0 恢复出帧
