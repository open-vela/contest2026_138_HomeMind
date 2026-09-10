# 2026-09-10 摄像头冻结根因：WP C CLI 静态栈（已恢复出帧）

状态：**部分完成（摄像头采集已恢复）**。端侧 TFLM `vision local` 推理仍有待验证/可能挂起；本轮已重新取得真实 OV2640 帧。

## 1. 结论

| 项目 | 内容 |
| --- | --- |
| 硬件 | **正常**。用户判断正确。09-03 `8cf605d9` 与 09-06 归集版 `c8a373e6` 均可稳定出帧。 |
| 卡点性质 | `media_probe` / `open("/dev/video0")` 异步硬锁；与互斥死锁、网络无关。 |
| 真正根因 | **WP C（d195fc3）把 CLI 线程改成 64KB `static uint8_t g_cli_stack[]`（.bss/DRAM）后**，摄像头打开路径在该栈上硬锁。 |
| 修复 | 将 `nsh_commands_start()` 回退为 `agent_task_create(...)`（PSRAM 栈）。**不回退** person_detect / vision local 代码。 |
| 验证 | 恢复后 `media_probe` 连续 2/2：`MEDIA_VIDEO_FRAME bytes=153600` + `MEDIA_AUDIO_PCM bytes=640` + `MEDIA_PROBE_DONE`。 |

**一句话**：不是 OV2640/I2C/硬件坏了，是 WP C 为「PSRAM 栈在 cache-suspend 不可达」改的 DRAM 静态栈与摄像头驱动路径冲突；换回 `agent_task_create` 后立刻恢复。

## 2. 二分证据（烧录实测）

| BIN | 来源 | `media_probe` |
| --- | --- | --- |
| `8cf605d9…` | git `39cb986` 09-03 媒体探针 | **PASS** 153600B |
| `c8a373e6…` | git `269ab85` 09-06 归集媒体 | **PASS** 153600B |
| `81c47e67…` | git `d195fc3` WP C | **FAIL** 卡在 `MEDIA_PROBE_BEGIN` 后 |
| `ffe989ec…` | 本轮：TFLM 保留 + CLI 回退 | **PASS** 2/2 153600B |

二分范围：`269ab85`→`d195fc3` 的固件相关改动只有 Makefile TFLM、`person_detect*`、`nsh_commands.c`（含 **CLI 栈** 与 capture 常驻缓冲）。`build.sh` 无 diff。回退 CLI 栈后即恢复。

## 3. 旁证

- 冻结时 CLI 栈地址：`0x3fcabe50`（内部 DRAM，`g_cli_stack`）。
- 恢复后 CLI 栈地址：`0x3c2f1140` / `0x3c2c6d00`（PSRAM，`agent_task_create`）。
- 跳过第二次 XCLK、I2C 初始化顺序调整均**不能**解除冻结（已试，仍卡在 `i2c_init_enter` 附近）。
- 降 XCLK 无效（因根因不在时钟）。

## 4. 本轮改动（可回退）

- `/home/hfy/work/backups-20260910/`：补丁前备份。
- `firmware/ai_agent_overlay/src/channels/nsh_commands.c`：仅回退 `nsh_commands_start()`；保留 `vision local` / 常驻帧缓冲 / `pthread.h`。
- `firmware/nuttx_media/esp32s3_board_camera.c` + live `nuttx/boards/.../esp32s3_board_camera.c`：恢复 git HEAD（去掉 XCLK skip 等临时补丁）。
- `nuttx/arch/xtensa/src/esp32s3/esp32s3_i2c.c`：恢复 09-09 备份（去掉 I2C 顺序实验 + 诊断打印）。
- 脚本：`transfer/restore_cam_wpc_20260910.py`、`transfer/patch_cam_xclk_20260910.py`（后者已被撤销）。
- 当前产物 SHA：`ffe989ec64bad9ee1d0b1fc4f33707d33adced45dc66b0b4bf96b68a3e937af8`（`artifacts/nuttx.bin`）。

## 5. 仍待处理

1. **`vision local` 首次实测无回显且可能导致串口写超时**——TFLM init 路径需单独排查；摄像头采集本身已恢复。
2. WP C 原注释中的「cache-suspend 窗口 PSRAM 栈不可达」风险仍在；若 `vision local` 稳定性不足，需在**不把 64KB CLI 栈放进 DRAM**的前提下做栈方案（例如单独工作线程 + 内部 SRAM 小栈仅覆盖 flash 临界区）。
3. v4l2 阶段诊断打印仍在 live 驱动中，验收前可清。
4. 未 Git commit / 未 push。

## 6. 复用

- 烧录：`ESPTOOL_PYTHON=$(command -v python3) ./scripts/build.sh flash`，`SERIAL_PORT=/dev/homemind-esp32`。
- 探测：串口发 `ai_agent` → `media_probe`；期望 `MEDIA_VIDEO_FRAME bytes=153600`。
- 对照旧固件：`git show <sha>:artifacts/nuttx.bin > /tmp/x.bin` 后 esptool 写 `0x0`。
