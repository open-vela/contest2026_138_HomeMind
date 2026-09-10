# 2026-09-10 端侧视觉 WP C 状态（诚实边界）

固件：`2888207a…`（交换类别索引后）/ 前一版 `d786e062…`（crop 统计+均匀帧拒绝）。

## 已验收（管道）

| 项 | 结果 |
| --- | --- |
| `/dev/video0` 连续 open/capture | 通过（诊断打印已清） |
| TFLM INT8 init | arena=139264，AllocateTensors OK |
| 96x96 灰度预处理 | 有 crop mean/std/min/max 日志 |
| Invoke + 反量化 | scale=0.003906 zp=-128 |
| `media_probe` 并行 | 仍可出帧 153600B |
| MQTT 事件代码路径 | 已接 `mqtt_channel_send`（当前未配 broker，显示 offline） |

## 未达验收（模型判别）

**不能宣称 20 正/20 负通过。**

| 条件 | crop 统计 | 旧索引 score | 交换索引后 score |
| --- | --- | --- | --- |
| 遮挡镜头 | mean≈-106 std≈2.8 | 0.55–0.66 DETECTED（假阳） | 被 std&lt;5 拒绝 |
| 真实场景/目标 | mean≈-17~15 std≈37–42 | 0.32–0.36 none | 0.19–0.65 不稳定 |

- TFLM 官方 `person_detect` 演示模型对 OV2640 灰度输入**判别力不足**：暗平坦帧曾高分，纹理场景反而低分。
- 已做工程缓解：均匀帧 `std<5` 拒绝；交换类别索引；tensor params 反量化。
- **缺口保留**：需更合适模型/训练，或明确接受“仅演示推理链路，不宣称人员检测准确率”。

## 复现

- 批量：`python3 /home/hfy/batch_vision.py N label`（早期 exit on person=）
- 日志：`/tmp/vision_batch_*.log`
- 备份：`/home/hfy/work/backups-20260910/`
- 铁律：`deploy-to-vm.sh` 必须包含 `src/vision/*`，否则改 person_detect 不生效。

## 下一步建议

1. 更换/导出更可用的 person 模型（或说明演示边界）；
2. 联网后验证 MQTT `person_detected` 入库；
3. 连续音频与离线唤醒（工作包 D）。
