# PCM 多缓冲/重入队故障诊断记录（2026-09-07，工作包 A）

## 结论摘要

**状态：未修复，保留阻塞。** 当前固件以"单缓冲有限采样（640 B/次，逐次开→配→收→停→关）"为已验收路径；多缓冲流式采集与同一 APB 重入队在驱动/上半层生命周期上仍有问题，不宣称完成。

## 实机现象（2026-09-07 复核，固件 c8a373e6）

单缓冲保底路径实机通过（/dev/ttyACM0，网关保持 active，探针未干扰网关）：

```
MEDIA_VIDEO_FRAME bytes=153600 checksum=592061891 sequence=0
MEDIA_AUDIO_STAGE config
MEDIA_AUDIO_STAGE info nbuffers=4 buffer=4095 ret=0
MEDIA_AUDIO_STAGE alloc count=1
MEDIA_AUDIO_STAGE enqueue count=1
MEDIA_AUDIO_STAGE start
MEDIA_AUDIO_STAGE poll
MEDIA_AUDIO_PCM bytes=640 buffer=0 checksum=3065632197
MEDIA_PROBE_DONE
```

历史现象（2026-09-03，`docs/evidence/2026-09-03_media_pause.txt`）：
- 四个缓冲实验：仅首个缓冲完成（DMA 中断正常、缓冲转移到 done），后续缓冲未完成。
- 同一 APB 重入队实验：`reenqueue cycle=1 ret=0`（入队 ioctl 返回成功），但 `drain cycle=1 ret=1 revents=0x9 errno=0` 失败，`apb->nbytes` 未更新。

## 源码根因分析（基于 Ubuntu 干净工作区 2026-09-07 源码）

### 候选根因 1（下驱动 nbytes 统计）：`esp32s3_i2s.c` `i2s_rx_worker`

```c
dmadesc = bfcontainer->dma_link;
bfcontainer->apb->nbytes = 0;
while (dmadesc != NULL && (dmadesc->ctrl & ESP32S3_DMA_CTRL_EOF))
  {
    bfcontainer->apb->nbytes += (dmadesc->ctrl >> ESP32S3_DMA_CTRL_DATALEN_S)
                                & ESP32S3_DMA_CTRL_DATALEN_V;
    dmadesc = dmadesc->next;
  }
```

- `esp32s3_dma_setup()` 只在 **TX** 描述符软件置 `ESP32S3_DMA_CTRL_EOF`；**RX 描述符的 EOF 位由硬件在传输完成时写回**（注释原文："suc_eof ... is set by software only in transmit descriptor"）。
- 单缓冲 640 B：`dma_size`（RX 默认 `ESP32S3_DMA_BUFFER_MAX_SIZE`）> 640，只生成 1 个描述符 desc0，硬件把 EOF 与数据长度写回 desc0 → worker 循环第一轮即命中，`nbytes=640` 正确。
- 多缓冲（探针 `buffer_size=4095`，`CONFIG_I2S_DMADESC_NUM=2`）：4095 B 被拆成 desc0（4092 B 上限）+ desc1（余量）。**EOF 只写回最后一个描述符（desc1）**；worker 从链头 desc0 开始，`desc0->ctrl & EOF == 0` → 循环一次都不执行 → `nbytes=0`。缓冲虽已转移到 done 且 poll 收到事件，但用户侧读到 0 字节 → drain 失败。

### 候选根因 2（上驱动生命周期）：`nuttx/audio/audio.c` head/tail 状态

- `audio_poll()`：`if (priv->head - upper->status->tail != upper->periods)` 立即 `POLLIN|POLLOUT`，若 `head - tail <= 0` 附加 `POLLERR`。
- 保底路径每次 cycle `open → … → close`。重新 open 后 `priv->head` 从 0 重新计数，而 `upper->status->head/tail` 为驱动全局累计值 → 重开后 `head - tail` 可能为负，poll 立即返回 `POLLIN|POLLERR`（=0x9）而数据并未就绪。这与 09-03 re-arm 实验 `revents=0x9` 现象一致：**事件与 apb->nbytes 数据不同步**。
- 同一 APB 重入队时，`audio_enqueuebuffer()` 的 `u.buffer` 分支直接调用下驱动 `enqueuebuffer` 并 `status->head++`，未校验下驱动 `rx.pend/rx.act` 是否仍有残留描述符或引用未释放（09-03 路径是重新 open 后复用同一 APB 内存，上一轮 `STOP/FREEBUFFER` 后下驱动 `rx.done` 清理与上驱动 `tail` 推进的时序竞态）。

### 候选根因 3（下驱动 EOF 匹配，低概率）：`i2s_rx_schedule`

```c
bfdesc = bfcontainer->dma_link;
while (bfdesc->next != NULL && (bfdesc->next->ctrl & ESP32S3_DMA_CTRL_EOF))
  {
    bfdesc = bfdesc->next;
  }
if (bfdesc == inlink) { ...移到 done... }
```

- 该循环意图定位"链上最后一个带 EOF 的描述符"并与中断报告的 `DMA_IN_SUC_EOF_DES_ADDR` 比较。
- RX 多 desc 场景下，EOF 由硬件写回最后一个 desc，此匹配在正常完成时成立；但若硬件把 EOF 写回的是链尾而 `next->ctrl` 尚未刷新（cache/时序），或中断发生在 EOF 写回完成前，`bfdesc` 停在链头导致与 `inlink` 失配 → 缓冲卡在 `rx.act`，后续缓冲永不启动。此候选未被实机单独验证，保留为次要嫌疑。

## 修复方向（后续工作包，不在 09-07 验收内）

1. **worker nbytes 统计改为遍历整条描述符链**（累计到 EOF 描述符为止，但不要求链头带 EOF），或在 `i2s_rxdma_setup` 时记录实际 desc 数并在 worker 中按 count 累加。
2. **探针/保底路径保持单缓冲**（现状已验收）：每 640 B 块完整走 open→config→alloc→enqueue→start→drain→stop→free→close，规避上驱动全局 head/tail 错位与多 desc 统计缺陷；voice 录制与 ASR/MiMo 链路均基于此路径。
3. **如需真流式**：上驱动在 open 时重置本会话 head/tail 记账，或在 close 后清零全局 status；下驱动增加 close 钩子清空 `rx.pend/rx.act/rx.done` 并释放引用，消除跨会话残留。

## 验收口径（工作包 A）

- 多缓冲完成/重入队现象：**已记录**（本文档 + 09-03 证据），**未修复**，按计划**保留阻塞**。
- 单缓冲有限采样：实机复核通过（见上）。
- 音频配置证据：`CONFIG_ESP32S3_I2S0=y/ROLE_MASTER=RX`，BCLK/WS/DIN=41/42/2，16 kHz/16 bit，`CONFIG_I2S_DMADESC_NUM=2`、`CONFIG_ESP32S3_I2S_MAXINFLIGHT=4`，`CONFIG_AUDIO/AUDIO_I2S/AUDIO_FORMAT_PCM=y`（scripts/build.sh apply_media_config）。
