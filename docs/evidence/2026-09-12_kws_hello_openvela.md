# 2026-09-12 KWS「你好 openvela」初版验收

固件 BIN：`fec95a8fe5f10a0ccfe56c3e29c533092b7695c949a04e990d4cfe3cbf1c561d`

## 命令

```
ai_agent
wake_kws [阈值]    # 默认 0.55；实测可用 0.45
```

流程：屏幕「LISTEN 3S...」→ 采 3s PCM → 8 维特征 → 逻辑回归 → LED 闪 3 次。

## 模型

- 特征：40 帧 RMS/ZCR/peak 的 mean/std/max + 首尾差 + 峰谷差（8 维）
- 样本：主机 `/home/hfy/kws_data/` **7 正 + 2 负**（板端部分 0 字节文件已跳过）
- 训练 acc≈0.89（样本极少，**严重欠采样**）
- 权重导出：`firmware/ai_agent_overlay/src/vision/kws_model_data.*`

## 实测

| 条件 | score | 结果 |
| --- | --- | --- |
| 对麦克风说话 | 0.480 | **HIT + LED**（thr=0.45） |

## 踩坑（明日勿重复）

1. **`int got` 未初始化** → fwrite 0 字节/ferror=1。已改 `got=0`。
2. **littlefs 一次性 fwrite 96000 可能失败**；成功样本为早期写入。
3. **采样后必须 quit+sleep**，连续 wake_rec 会卡死。
4. **串口 kws_dump 换行**曾是字面量 `\n`；部分 host 文件 0 字节。
5. 负样本（静音）能量与正样本接近 → 线性模型判别弱；需重录真静音 + 更多正样本。
6. 120 维 mel 特征在板端 Goertzel 与训练 FFT 不一致 → score=0；改为 8 维时域特征后打通。

## 边界（报告）

- **不是** 20 次分类统计；样本 7+2，仅证明「采样→训练→板端打分→LED」链路。
- 阈值 0.45 可能误触发；需更多负样本后重标定。
- 能量 VAD `wake_loop` 仍是独立已验收路径。

## 复现

- 训练：`python3 transfer/train_kws8.py`（PYTHONPATH 含 `~/.local`）
- 集成：`patch_wake_kws.py` + `patch_kws8.py`
- 部署清单已加 `kws_model_data.*`
