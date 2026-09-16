# 2026-09-16 I2S RX 配置取证：寄存器原文 + 厂商 HAL 对账 + 旋钮扫描

> 目的：终结"麦克风采样率到底是 16k 还是别的"这场拉锯。
> 做法：不再从 PCM 反推，而是把 **RX 寄存器原文** 打出来，与仓库自带的
> ESP-IDF 参考实现（`esp-hal-3rdparty/components/hal/i2s_hal.c`）逐条对账，
> 再用运行时旋钮扫配置。
> 工具：补丁 `0006-homemind-esp32s3-i2s-rx-regdump.patch` + 端侧 `i2sknob` 命令。

## 1. RX 寄存器原文（板上实测）

```
[I2SDBG rxstart] port=0 dw=16 slot=2 rate=16000 mclk=4096000 ch=1 role=0 mode=0
[I2SDBG rxstart] RX_CONF=00089600 CONF1=2f3de38f TDMCTRL=0000ffff
                 CLKM=14000027 CLKDIV=003c0001
[I2SDBG rxstart] TDM_EN=1 WS_WIDTH=15 BITS_MOD=15 CHAN_BITS=15 HALF_BITS=15
                 TOT_CHAN=0 MONO=0 SLAVE=0 MSBSHIFT=1 BIGEND=0
[I2SDBG rxstart] BCK_DIV=7 CLKM_DIV=39 GDMA_IN0=00000000 GDMA_IN1=0000000c
```

推导：`sclk=160MHz`、`mclk_div=160/4.096=39`、`bclk_div=mclk/bclk=4096000/512000=8`
⇒ `BCK_DIV=7`、`CLKM_DIV=39` ⇒ **BCLK = 512 kHz**，与 `rate×slot×dw = 16000×2×16` 自洽。

## 2. 与厂商 HAL 逐条对账

`i2s_hal.c` 的 `i2s_hal_tdm_set_rx_slot()`（Philips 模式）：

```c
i2s_ll_rx_set_ws_width(hal->dev, (total_slot * slot_bit_width) / 2);
i2s_ll_rx_set_half_sample_bit(hal->dev, total_slot * slot_bit_width / 2);
i2s_ll_rx_set_chan_num(hal->dev, total_slot);
i2s_ll_rx_set_active_chan_mask(hal->dev, slot_mode == MONO ? I2S_TDM_SLOT0 : slot_mask);
```

而 `esp32s3/include/hal/i2s_ll.h` 里这两个 setter 都**自带减一**：

```c
static inline void i2s_ll_rx_set_ws_width(i2s_dev_t *hw, int width)
{ hw->rx_conf1.rx_tdm_ws_width = width - 1; }
static inline void i2s_ll_rx_set_half_sample_bit(i2s_dev_t *hw, int half_sample_bits)
{ hw->rx_conf1.rx_half_sample_bits = half_sample_bits - 1; }
```

代入 `total_slot=2, slot_bit_width=16`：

| 字段 | 厂商算式 | 厂商落值 | NuttX 实际 | 结论 |
|---|---|---|---|---|
| `RX_TDM_WS_WIDTH` | `(2*16)/2 = 16` −1 | 15 | 15 | ✅ 一致（补丁 0005 得到反证支持） |
| `RX_HALF_SAMPLE_BITS` | `2*16/2 = 16` −1 | 15 | 15 | ✅ 一致 |
| `RX_BITS_MOD` | `data_bit_width−1` | 15 | 15 | ✅ 一致 |
| `RX_TDM_CHAN_BITS` | `slot_bit_width−1` | 15 | 15 | ✅ 一致 |
| `RX_MSB_SHIFT` | Philips=1 | 1 | 1 | ✅ 一致 |
| **`RX_TDM_TOT_CHAN_NUM`** | `total_slot−1` | **1** | **0**（从不写） | ❌ **唯一偏差** |

即：TX 侧 `i2s_txchannels()` 会写 `I2S_TX_TDM_TOT_CHAN_NUM`，RX 侧 `i2s_rxchannels()`
一个字都不写，寄存器停在复位默认值 0（"总 1 通道"），与 `i2s_set_clock()`
按 `total_slot=2` 推 BCLK 的假设冲突。

## 3. 运行时旋钮扫描（板上实测）

补丁 0006 挂了 5 个全局旋钮（默认 -1 = 保持原行为），端侧 `i2sknob` 可改，
做到"一次构建、多组配置"：

| 配置 | `TDMCTRL` | `[STRM-RAW]` 前 24 个 int16 | `dup`/`zero` | `eff_rate` |
|---|---|---|---|---|
| `totchan=1`（厂商值） | `0001ffff` | `-4 0 -17 0 -17 0 -24 0 -24 0 -132 0 …` | 0/0 | 14006 |
| `totchan=1 half=7` | `0001ffff` | `306 16384 0 0 0 0 …` | **144**/0 | 15570 |
| `totchan=0`（现状基线） | `0000ffff` | `0 0 7 0 7 0 4 0 4 0 -1 0 …` | 0/0 | 15570 |
| `totchan=3`（帧长 64 BCLK） | `0003ffff` | `-4 0 -4 0 -8 0 -8 0 …` | 0/0 | 15518 |
| `bitsmod=chanbits=7` | `0001ffff` | `-513 6 0 0 0 0 …` | **144**/0 | 15570 |

三个结论：

1. **`TOT_CHAN` 从 0 → 1 → 3，`[s,0,s,0]` 结构完全不变。** 帧长/slot 数
   **不是**重复的成因，"补上 TOT_CHAN 就能修好"被实测否掉。
2. `BITS_MOD / CHAN_BITS / HALF_BITS` 改成 7 会让 RX **直接停住**
   （`dup=144/146`，只剩一个 `16384` 尖峰）⇒ 这三项必须保持 15。
   这同时说明 `HALF_BITS=15` 是对的（而不是我一度怀疑的 "应为 7"）。
3. `chunks=146 / steady_ms=2980` = **20.4 ms/块** ⇒ `640B/20.4ms = 31373 B/s
   ≈ 15686 int16/s ≈ 16000`，且 `zero=0 dup=0`。

## 4. 推翻的三条旧判断

- ❌ "`eff_rate=16063 Hz` 是消费循环限速出来的假值。"
  实测消费端**没有**限速：生产侧 20.4 ms/块慢于消费侧的 `usleep(10000)`=1 tick，
  `[STRM]` 的 `gap_max` 只有 60–70 ms 且 `tmo=0`。**上报速率是真实的。**
- ❌ "把 `usleep(10000)` 换成 `poll(POLLIN)` 就能让消费端不再拖后腿。"
  实测该驱动会在第 2 块起立刻回"POLLIN 但 `apb->nbytes=0`"（因为每轮都新分配
  APB，完成队列头部不是本轮这个），随后缓冲池耗尽、`AUDIOIOC_ALLOCBUFFER`
  永久阻塞 —— 卡在第 5 块。已回退为 `usleep(10000)` 并把这个结论写进代码注释。
- ❌ "RX 每帧 2 slot、右 slot 恒静音 ⇒ 必须去交错"（更早的中间结论）。
  `[STRM-RAW]` 显示的是**成对重复 + 夹零**，不是"一路信号 + 一路零"。
  端侧主机侧去交错会砍掉一半真实样点。

## 5. 主机侧复核（对 14 段线上抓回的 PCM）

`transfer/_pcm_struct_exam.py` / `_pcm_deint_test.py`：

- `x[n]==x[n+2]` 占 **80.7%**，而 `x[n]==x[n+1]` 只有 8.3% ⇒ 周期 2 的重复结构。
- 频谱关于 `Fs/4`(=4 kHz) **折叠**，镜像相关 **0.986**，分带能量呈完美 V 形
  （0–500 Hz 与 7500–8000 Hz 同为峰值，4000–4500 Hz 为谷）⇒ 零插入的指纹。
- `raw[0::4]` 是**平滑自然的波形**（`-796 -817 -408 -181 -13 207 451 347 …`），
  镜像降到 −0.017；`raw[0::2]` 仍带精确重复。⇒ 每个真实样点占 **4 个 int16**。

`transfer/_asr_sweep.py`（直接调本地 faster-whisper 做裁判，12 种重构 × 4 句真值）：

| 重构 | 命中 |
|---|---|
| `raw @16000`（现状） | **1/3 可用句**（"打开书房的空调"→ 听出"…空调…"） |
| 其余 11 种（d2/d4/d8 × 各速率） | 0/3 |

⇒ 现有"照 16 kHz 单声道直接上报"在**音高/时序上是对的**，糊掉的成分来自
重复+夹零的畸变与丢失的样点，而不是速率错配。**把 PCM 抽稀反而更差。**

## 6. 当前结论与下一步

**结论**：NuttX 的 ESP32-S3 I2S RX 路径对 16 位数据宽度的处理有问题 ——
DMA 缓冲里拿到的是 32 位 FIFO 字，其中**只有一个 16 位半字带数据、另一半是填充**，
且**相邻两个字是重复的**。所以 App 的 int16 视图是"每个真实样点 4 个字"，
同时丢掉了约一半的音频。参照 ESP-IDF：它在 ISR 里按 `bits_per_sample`
把 32 位 FIFO 字**拆包**成 16 位样点；NuttX 驱动则把原始 FIFO 字直接 DMA 出去。

**下一步（按性价比排序）**：

1. 驱动侧按 ESP-IDF 的做法拆包（`i2s_ll_rx_get_fifo_data` + 按
   `data_width` 复制），这是正解，但要动 GDMA 的 `IN_CONF`（当前 `0x0c`）。
2. 先把厂商确认缺失的 `TOT_CHAN_NUM = total_slot − 1` 补上（虽然不解决重复，
   但它是与厂商参考实现唯一不一致的地方，属于必须修的债）。
3. 补一步对照实验定"真实音频速率"：让小爱播一段**已知基频**的音频，
   用 F0 反推。现有数据里 F0 估计值为"4000 Hz 下 143 Hz / 8000 Hz 下 286 Hz"，
   二者都在人声范围内，仅凭 F0 不足以定案，需要可控音源。

## 7. 复现方式

```bash
# 1) 构建（会自动 deploy，含补丁 0006 的幂等应用与"标记恰好 1 次"断言）
cd $OPENVELA_ROOT/contest2026_138_HomeMind/scripts && JOBS=2 ./build.sh build

# 2) 刷写
export ESPTOOL_PYTHON=$(command -v python3) SERIAL_PORT=/dev/homemind-esp32
JOBS=2 ./build.sh flash

# 3) 取寄存器 + 真实速率 + 原始样点
python3 transfer/tr_plan.py 60 "8:kws_listen stop" "18:audio_stream 3"

# 4) 扫配置（旋钮：half bitsmod chanbits ws totchan dbg，-1 = 原行为）
#    i2sknob -1 -1 -1 -1 1   # = 厂商的 TOT_CHAN 值
```

## 8. 新增/改动的文件

- `firmware/patches/0006-homemind-esp32s3-i2s-rx-regdump.patch`（新增，5 hunk）
- `tools/deploy-to-vm.sh`：接入 0006 的 `ensure_marker_patch` + 闭合标记断言
- `firmware/ai_agent_overlay/src/channels/nsh_commands.c`：
  `[STRM-RAW]` 原始样点自报、`tmo/gap_max/poll_wait` 计数、`i2sknob` 命令
- `transfer/_pcm_struct_exam.py`、`_pcm_deint_test.py`、`_pcm_rate_verdict.py`、
  `_pcm_replay_check.py`、`_asr_sweep.py`、`_asr_probe2.py`
