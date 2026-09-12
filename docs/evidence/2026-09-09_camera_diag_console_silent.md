# 2026-09-09 夜间：摄像头诊断镜像 + 板子串口彻底静默

状态：**阻塞（需现场断电重启）**。本文只记录实测证据与已排除项，不升级任何模块状态。
端侧有人/无人识别仍为 **未实现**；本轮没有取得推理结果，也没有跑成 `media_probe`。

## 1. 本轮做了什么

1. 给 `nuttx/drivers/video/v4l2_cap.c` 的 `capture_open()` 加**可观测的有界 trylock 探针**
   （诊断用途，不是修复）：加锁前打印 `nxmutex_is_locked()` / `nxmutex_get_holder()`，
   再用最多 60 次 `nxmutex_trylock()`（每 100 ms 一次）取代原来盲等的
   `nxmutex_lock()`，把 `rc / attempts / holder` 同时打到串口和 syslog。
   这样能三分判定：锁被谁持有（死锁）／锁空闲（卡点在更下游）／根本没走到 trylock。
2. 曾给 `hm_lcd_display.c` 加 1 Hz 红/绿心跳方块用于区分"整片硬锁"与"仅 Wi-Fi 挂"，
   **因刷入后板子异常，该改动已回退**（见第 3 节）。
3. 构建 → 刷写 → 多次串口探查。

## 2. 构建与刷写（可复核）

| 项 | 值 |
| --- | --- |
| 诊断镜像（含 trylock 探针 + LCD 心跳） | BIN `d791f28259b0804791c8ac0187e5fe8db4861d3f7c3bad98c7e4bc90edd4b949` |
| 回退 LCD 心跳后（**当前 artifacts**） | BIN `e1c33b48977e68bd358e7b44c68aaac035255ac099500288b244057d7fa8d4be` |
| ELF（对应回退版） | `1821528e8d8d0850a09641b37a45848f17b11e640ed5928e9eae969a6a696040` |
| 刷写 | `esptool v5.3.1`，`Hash of data verified`，1420308 B，hard reset |
| 构建日志 | `/home/hfy/work/build_camdiag_20260910.log`、`build_camdiag2_20260910.log` |
| 刷写日志 | `/home/hfy/work/flash_camdiag_20260910.log`、`flash_camdiag2_20260910.log` |

ELF 中已确认含 `[CAM-OPEN] lockstate` / `trylock rc` / `ABORT_BUSY` 字符串（16 处 `CAM-OPEN`），
排除"改了没编进去"。镜像头 `e9 02 02 20` 与历史镜像逐字节一致，`fix_esp32s3.sh` 四步全部应用。

**关键配置事实**：`CONFIG_SMP` 未开（单核）；`CONFIG_WATCHDOG`、
`CONFIG_ESP32S3_WDT/INT_WDT/TASK_WDT/MWDT` **全部未启用**——真死机不会被复位掩盖，
只能靠硬复位恢复。这也是历史上"冻结后只能 esptool 复位"的直接原因。

## 3. 板子现象：串口彻底静默

刷写后板子**完全没有任何串口输出**，且不再接入 Wi-Fi。多次不同方式复测均为 **0 字节**
（不是乱码，说明芯片根本没在 USB CDC 上发送）：

| 复测方式 | 结果 |
| --- | --- |
| pyserial 打开 `/dev/homemind-esp32` 读 6 s | 0 字节 |
| 复位后立刻抓 30 s / 40 s / 45 s（抢在启动打印前开端口） | 0 字节 |
| `timeout 8 cat /dev/homemind-esp32 \| od -c`（绕过 pyserial） | 空 |
| 连发 3 轮 poke + `net_status` + `help` | 0 字节（写入成功、无回显） |
| 网关 `homemind-gateway` 自己读串口（journal 日志） | 无任何板子输出 |
| ping `.248/.249/.252` | 全部无应答 |
| 全子网 ping 扫描 + ARP 查 MAC `a4:cb:8f:e1:e0:08` | **不在网内** |

## 4. 已排除的假设（逐条有据）

| 假设 | 结论 | 依据 |
| --- | --- | --- |
| LCD 心跳改动搞崩启动 | **排除** | 单独回退心跳后重建刷写，仍然 0 字节 |
| trylock 探针 / v4l2_cap.c 改动 | **排除** | 该改动只在 `capture_open()` 内执行，启动阶段不触及 |
| 我的诊断镜像本身有问题 | **排除** | 换 Git `HEAD` 历史镜像 `81c47e67…`（1 418 316 B）刷入同样 0 字节 |
| 摄像头 bringup（I2C 卡死）挂住启动 | **排除** | 换 `artifacts/nuttx.bin.media-nocamera-20260902`（无摄像头）同样 0 字节 |
| littlefs `/data` 被上次冻结写坏导致挂载卡死 | **排除** | `esptool erase_flash` 全片擦除后重烧，仍然 0 字节 |
| 镜像格式/头错误 | **排除** | 新旧镜像前 24 字节完全一致，`fix` 四步全部成功 |
| 芯片停在 ROM 下载模式 | **排除** | `esptool --before no-reset` **连接失败**（"No serial data received"），说明不在 bootloader |
| USB 反复重枚举（崩溃重启循环） | **排除** | 间隔 15 s 两次 `lsusb`，设备号稳定为 099 |
| 看门狗复位掩盖现场 | 不适用 | 全部看门狗配置均未启用 |

## 5. 当前判断与下一步

芯片**不在 bootloader**（esptool 不复位连不上），但也**没有任何应用输出**，且**不上 Wi-Fi**。
四个互不相同的固件 + 全片擦除都复现同一现象，因此判断为**板子侧的硬件/USB 状态问题**：
应用要么在 USB 控制台初始化之前就挂死，要么 USB-Serial-JTAG 外设处于芯片复位无法清除的异常态。
RTS 硬复位（esptool）无法恢复，ROM 下载模式仍可正常通信。

**下一步（需人工）：把开发板 USB 断电拔插一次（真正下电，不只是 RTS 复位），
然后回报屏幕上显示什么（HomeMind 图标界面 / 纯色 / 全黑）。**
下电恢复后按此顺序继续：

1. 先抓启动输出确认 `nsh>` / `vela>` 与 `net_status` 的 IP；
2. 重新刷入 `e1c33b48…`（含 trylock 探针、已回退 LCD 心跳）；
3. 跑 `transfer/probe_cam_diag_20260910.py`，按第 1 节的三分判定读
   `[CAM-OPEN] lockstate` / `try=` / `trylock rc=` / `ABORT_BUSY` 输出；
4. 只有 open/close 生命周期稳定后，才恢复真实帧探针与 TFLM 人员检测。

## 6. 遗留（本轮未做）

- 未执行 `media_probe` 之后的任何摄像头阶段判定；
- 未执行公网视觉命令，未上传任何原始音视频；
- 未执行 Git push / PR 合入 / 云端发布；
- Ubuntu 官方仓仍有未提交改动（诊断补丁、板级源、产物、文档），**不要重置或覆盖**。

## 7. 复用工具

- `transfer/patch_cam_diag_20260910.py` — 施加诊断补丁（带锚点唯一性断言 + 自动备份到
  `/home/hfy/work/backups-20260909/`）
- `transfer/revert_lcd_heartbeat_20260910.py` — 只回退 LCD 心跳，保留 trylock 探针
- `transfer/probe_cam_diag_20260910.py` — 独占串口探针（停/恢复网关、ping 对照、dmesg）
- `transfer/boot_capture.py` — 复位后立刻抓启动输出（避免错过早期打印）
- `transfer/ser_quick_check.py` — 最小串口状态探查

> 环境坑：本轮 `sudo` 必须用 `echo "$PW" \| sudo -S`；构建脚本的 `ESPTOOL_PYTHON` 默认指向已不存在的
> `.venv`，需显式传 `ESPTOOL_PYTHON=$(command -v python3)`；`scripts/build.sh build` 内部已包含
> `deploy_sources`，无需单独跑 deploy。
