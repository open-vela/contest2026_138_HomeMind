# 2026-09-10 离线唤醒（能量 VAD）+ 本地 LED

固件：`6655b16bd6e073f4162bf91d21406ff992f7ba7bf59db9d3b9c6c4f2c7037dc6`

## 命令

```
ai_agent
wake_loop [秒] [阈值] [hold]
# 例：wake_loop 8 1500 3
```

- 连续 I2S 采集（`2026-09-10_i2s_continuous_pcm.md` 配方）
- 每 640B 算 RMS；连续 `hold` 块 ≥ 阈值 → **WAKE**
- 触发后板载 LED `/dev/gpio0` 闪 3 次（本地命令，断网可用）
- 打印明确标注 **energy VAD，不是「你好，openvela」KWS 模型**

## 实测（8s，thr=1500，hold=3）

| 条件 | 结果 |
| --- | --- |
| 对麦克风说话 | **4 次 WAKE + LED**（rms≈1831–2423） |
| 静音（前序 4s，thr=8000） | 0 wake；rms 多为 0，偶发 900–2000 噪声尖峰 |

## 边界（必须写进报告）

1. **不是**指定词「你好，openvela」识别；任何足够响的语音/噪声都可能触发。
2. 未做安静 / 背景说话 / 断公网 各 20 次统计。
3. RMS 偶发 `nan`（平方和溢出，需改 double 累加）。
4. 官方 `keyword_scrambled` 模型与中文唤醒词无关；真 KWS 需自备中文模型。

## 下一步（真 KWS）

- 导出/训练中文微型 KWS（你好 openvela）INT8 tflite → 替换 `hm_wake_score()`
- 或板端录制样本后在主机量化再烧录
- 仍复用当前连续采集循环

证据命令：`wake_loop 8 1500 3`；备份 `/home/hfy/work/backups-20260910/`
