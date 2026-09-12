# KWS「你好，openvela」— 暂停交接（2026-09-10）

## 目标

真关键词唤醒（非能量 VAD）。板端采样 → 主机 numpy 训练 INT8 线性模型 → 板端推理 + LED。

## 已完成

1. **`wake_rec pos|neg <i> [sec]`** 已进固件，写 `/data/kws_*.pcm`（s16le 16k mono，约 96000B/条）。
2. **板端样本 16/16 齐**（`ls /data` 已确认）：
   - `kws_pos_01..08.pcm`（说「你好，openvela」）
   - `kws_neg_01..08.pcm`（静音）
3. **主机已装 numpy 2.2.6**（阿里源，`~/.local`）。
4. **训练脚本** `transfer/train_kws.py`：log-mel 16 带 → 64 维统计特征 → 逻辑回归 → 导出 `kws_model_data.h/.cc`（int8 权重）。
5. **`kws_dump pos|neg <i>`** 已进固件（BIN 含 `d1eb1caf…` 之后），串口 base64 导出。
6. 采集铁律：**每条采样后 `quit` + 等 3s 再进 agent**，否则第 2 条易卡死；卡死后拔插 USB。

## 卡点（明天先做）

### 串口导出不可靠
- `kws_dump` 里 `printf("%s\\n")` 在 raw-string 补丁里变成了**字面量 `\n`**，不是换行。
- 部分解析成功：`/home/hfy/kws_data/` 下 **9 个 96000B** 文件有效，其余 0 字节：

| 文件 | 状态 |
|------|------|
| pos_01,03,04,05,06,07,08 | **96000 OK** |
| neg_01,02 | **96000 OK** |
| pos_02, neg_03..08 | **0 字节**（解析失败/超时） |

### 建议明日步骤（按序）

1. **修 `cmd_kws_dump`**：`printf("%s\n", b64)` 单反斜杠；或直接 `write(1,...)` 换行。
2. 重编译烧录（**只改 nsh_commands**，注意 deploy 含 vision）。
3. 重新 dump 缺的 7 个文件到 `/home/hfy/kws_data/`（每次 2～4 个，避免 SSH 超时）。
4. `python3 /home/hfy/train_kws.py`（先 `sys.path` 带 `~/.local/.../site-packages`）。
5. 把 `kws_model_data.*` 拷进 overlay `src/vision/`，**写入 `deploy-to-vm.sh` 的 `overlay_files`**。
6. `wake_loop` 或新命令里：能量够 → 取 3s 窗口 → 提特征 → 线性打分 → 阈值 → LED。
7. 说话 / 静音 各测若干次；写证据 md。

## 路径备忘

- 仓：`~/work/openvela-clean-20260830/contest2026_138_HomeMind`
- 板：`/dev/homemind-esp32`，`ESPTOOL_PYTHON=$(command -v python3)`
- 主机样本：`/home/hfy/kws_data/`
- 板端原始：`/data/kws_*.pcm`
- 备份：`/home/hfy/work/backups-20260910/`
- 证据目录：`docs/evidence/`
- 采样/导出相关：`transfer/patch_wake_rec.py`、`patch_kws_dump.py`、`train_kws.py`、`cmd_dump_*.txt`

## 边界（报告口径）

- 能量 VAD `wake_loop` 已验收（见 `2026-09-10_wake_loop_energy_vad.md`）。
- **「你好，openvela」KWS 未完成**，勿写成已实现。
- 样本仅 8+8，训练结果仅为初版，不足 20 次分类统计。

## 固件

当前板上：含 `kws_dump` 的 `d1eb1caf…`（或更新）。`wake_rec` / `audio_stream` / `wake_loop` / 视觉混合检测均在。
