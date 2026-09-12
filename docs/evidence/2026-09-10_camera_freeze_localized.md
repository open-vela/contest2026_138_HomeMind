# 2026-09-10 摄像头冻结卡点重定位（推翻「卡在 cmng->mutex」）

状态：**部分完成（诊断）**。端侧有人/无人识别仍为 **未实现**，本轮没有取得推理结果。
本文只记录实测证据，**修正了 2026-09-09 交接文档里的错误结论**。

## 1. 结论摘要

| 项目 | 09-09 交接的结论 | 本轮实测修正 |
| --- | --- | --- |
| 卡点 | `capture_open()` 等待 `cmng->mutex` | **错误**。锁完全空闲（`trylock rc=0 attempts=0`），一路走过了 mutex、IMGSENSOR_INIT 入口、XCLK 全部步骤 |
| 死锁性质 | 互斥死锁 | **不是死锁**。锁状态打印显示 `refs=0`、持有者为 -1；且卡在两句 `printf` 之间，中间只有 `config = priv->config;` / `this_cpu()` 这类无害语句 → **异步硬锁** |
| 真正阶段 | — | 死在 OV2640 传感器初始化：XCLK 启动后、`esp32s3_i2cbus_initialize()` 入口附近 |

**一句话**：不是软件在死等，是芯片在传感器初始化阶段被异步硬锁；串口打印只是被"截断了"，
造成"卡在某行之后"的假象——**这也是此前多次误判卡点的原因**。

## 2. 实测轨迹（BIN `37ab5eb2107d2ca855434f865b8990734ddf35af547eddcb0261bb2adf43e392`）

```
media_probe
MEDIA_PROBE_BEGIN video=/dev/video0 audio=/dev/audio/pcm_in0
MEDIA_VIDEO_STAGE open_enter
[CAM] 1
[CAM-OPEN] cmng=0x3fcd8f38 imgdata=0x3fccadf8 sensor=0x3fccaa1c open_num=0
[CAM] 2
[CAM-OPEN] trylock rc=0 attempts=0        ← 锁空闲，零次重试
[CAM] 3                                    ← 已获得锁
[CAM] 4
[CAM-SENSOR] init_enter
[CAM-SENSOR] xclk_init_enter
[CAM-SENSOR] xclk_init_exit pwm=0x3fccac70
[CAM-SENSOR] xclk_setup_enter / xclk_setup_exit
[CAM-SENSOR] xclk_start_enter / xclk_start_exit
[CAM-SENSOR] xclk_done                     ← 20 MHz XCLK 已启动（up_mdelay(50) 后）
[CAM-SENSOR] i2c_init_enter
[I2CBUS] lock_enter
[I2CBUS] locked refs=0                     ← I2C 私有锁正常拿到
[I2CBU                                     ← 打印被截断，芯片硬锁
```

再早一版（BIN `ad284345…`）同样停在 `i2c_init_enter`；更早一版（BIN `e1c33b48…`）停在
`[CAM-OPEN] mutex_enter …sensor=0x` 半行——**三次都卡在同一时间窗口，只是被截断的位置不同**。

## 3. 关键旁证

- **本次未配置 Wi-Fi**（`net_status` = no / 0.0.0.0）也照样冻结 → 与 Wi-Fi、ping、网络栈无关，
  09-09 记录的"ping 同时 100% 丢包"是整机硬锁的**结果**，不是网络故障。
- **`cmng->mutex` 未被持有**：`nxmutex_get_holder()` 返回 -1，`trylock` 首次即成功。
- **XCLK 在开机时已经启动过**且系统长期正常运行（`board_camera_initialize()` 里的
  `ov2640_start_xclk(0)`），说明"XCLK 本身"不致命；致命的是 `ov2640_init()` **第二次**启动 XCLK 之后。
- **无看门狗兜底**：`CONFIG_WATCHDOG`、`ESP32S3_WDT/INT_WDT/TASK_WDT/MWDT` 全部未启用 → 硬锁后不会自愈。
- 冻结后 50 s 内**没有重启打印** → 是挂死，不是 brownout 复位。

## 4. 本轮尝试过的修复（未奏效，已保留为可回退改动）

在 `esp32s3_i2cbus_initialize()` 中把 `i2c_init()` 移到 `irq_attach()` / `up_enable_irq()` **之前**
（原顺序是先开中断、后初始化硬件，存在"中断在控制器复位态被打开→中断风暴"风险）。
**结果：仍然冻结，且卡点不变** → 排除中断顺序因素。
该改动仍在源码中，备份于 `/home/hfy/work/backups-20260909/esp32s3_i2c.c.*`。

## 5. 仍然成立的环境结论（09-09 夜间）

- 板子曾出现**串口彻底 0 字节**（4 个不同固件 + 全片 `erase_flash` 均复现，RTS 复位无效），
  **必须拔插 USB 真正下电才能恢复**；此后 esptool 复位即可正常重启。
  详见 `2026-09-09_camera_diag_console_silent.md`。
- `set_wifi` 在 `vela>` 下会**同步阻塞**并曾导致冻结；配网建议改用 nsh 的
  `ifup` → `wapi mode/psk/ap/essid` → `renew` 路径。

## 6. 下一步建议（按性价比排序）

1. **硬件/板级方向**：确认 OV2640 的供电与信号完整性。重点看 20 MHz XCLK 起振后传感器开始输出
   PCLK/VSYNC/HREF/D0-D7 时的电流与引脚占用，排查是否与 PSRAM/Flash 引脚（GPIO33-37）冲突。
   这一现象无法再靠加打印定位——异步硬锁只会把打印截断。
2. **降级验证**：把 XCLK 降到 10 MHz 或 8 MHz，或在 `ov2640_init()` 里把 I2C 初始化提到 XCLK 之前，
   验证是否能越过该阶段（传感器无时钟时 I2C 会失败但应当"报错返回"而非挂死）。
3. **决定取舍**：摄像头是 WP C 的前置。若硬件方向短期无解，建议先明确记录缺口，
   把时间投向家庭服务收口、小程序真机、提交材料与 PR 合入（截止 2026-09-20）。

## 7. 本轮未做

- 未取得真实帧、未运行 TFLM 推理、未产生存在事件；
- 未执行公网视觉命令，未上传任何原始音视频；
- 未执行 Git push / PR 合入 / 云端发布；
- Ubuntu 官方仓仍有未提交改动（诊断补丁、I2C 顺序改动、板级源、产物、文档），**不要重置或覆盖**。

## 8. 复用工具（transfer/）

- `patch_cam_diag_20260910.py` — `capture_open` trylock 探针（证明锁空闲）
- `patch_cam_stage_lcd_20260910.py` — LCD 阶段号 + `[CAM] n` 短标记（死后读屏）
- `patch_i2c_init_order_20260910.py` — I2C 硬件初始化前置于开中断 + `[I2CBUS]` 细分标记
- `revert_lcd_heartbeat_20260910.py` — 回退 LCD 心跳
- `hm_cmd.py`（`HM_NEED_VELA=1 HM_CMDS="cmd|秒"` 批处理）、`boot_capture.py`（只读抓启动）、
  `ser_quick_check.py`（最小探查）、`probe_cam_diag_20260910.py`（含 ping 对照的完整探针）
