# 2026-09-10 端侧视觉：混合存在检测（可用）

固件 BIN：`a6dac58863e86a619f29a1f442b7fc054ed3348a9dc49540a4acb7d8ca44b8ee`  
Wi-Fi：已恢复 `192.168.31.252`。

## 方案

官方 `person_detect`（MobileNetV1 INT8）单独判别不足。现改为 **混合存在检测**：

1. 全帧下采样 96x96 灰度（非中心裁剪）
2. 均匀帧拒绝（遮挡 `std<5` → score=0）
3. 特征：肤色比 / 边缘能量 / 帧差运动
4. 仍跑 TFLM Invoke（保留 AI 推理链路）
5. `hybrid = 0.55*skin_n + 0.20*edge_n + 0.15*mot_n + 0.10*tflm_p`，默认阈值 **0.35**

## 实测（10+10）

| 条件 | 结果 | score |
| --- | --- | --- |
| 镜头前有目标 | **9/10 DETECTED** | min 0.327 max 0.564 mean 0.474 |
| 遮挡镜头 | **10/10 none** | 全部 0.000（std=4 拒绝） |

延迟：有目标约 4.16 s/帧（含 TFLM Invoke）；遮挡被 early-reject，约 0 ms。

## 边界（必须写进报告）

- 这是 **存在/有目标** 检测，不是完整人体检测器；肤色/运动弱时可能漏检（正样本第 8 帧 0.327）。
- 空场景（非遮挡）未单独做 10 次统计；遮挡是最强负样本。
- TFLM 分类头单独不可靠，仅作辅助 10% 权重。
- MQTT `person_detected` 代码已接，设备侧 broker 未配置时显示 offline。

## 复现

- `vision local [threshold]`；批量 `/home/hfy/batch_vision.py`
- 日志：`/tmp/vision_batch_pos_hyb_*.log`、`neg_hyb_*.log`
- 源码：`firmware/ai_agent_overlay/src/vision/person_detect.cc`（须经 `deploy-to-vm.sh` 的 `overlay_files`）
