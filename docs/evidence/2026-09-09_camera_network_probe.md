# 2026-09-09 摄像头与网络响应对照

状态：**部分完成（诊断）**。端侧有人/无人识别仍为**未实现**，本轮未取得推理结果。

## 版本与环境

- Ubuntu：`192.168.31.251`；开发板：ESP32-S3-EYE。一次 watchdog 复位后 USB 串口从 `/dev/ttyACM0` 重枚举为 `/dev/ttyACM1`，第二阶段探针显式使用 ACM1；随后安装 udev 别名 `/dev/homemind-esp32`，网关改用该稳定路径。
- 当前 Ubuntu 官方工作区：`/home/hfy/work/openvela-clean-20260830/contest2026_138_HomeMind`，HEAD 为 `aaefdcb`，工作区含本轮诊断未提交改动。
- 当前正式诊断构建 BIN SHA-256：`d2ef3ee790f2036a4aa6745f9e3fe2a0647f869d5a530c8d3f89da3c8caa6316`；对应 ELF 为 `8b87542fcd14961e95a81ea30630c5cfaeb72b05718b2c7ed023047f3f5028e4`。本轮 esptool 写入后报告 `Hash of data verified`。该镜像包含阶段标记，不等于人员检测推理已通过。
- 家庭 `homemind-api`、`homemind-gateway`、`homemind-relay` 三服务均为 `active`；`http://127.0.0.1:8001/v1/health` 返回 `{"status":"ok","service":"homemind-c1"}`。服务活跃不等于云端 relay 或完整业务链路验收通过。
- Windows 工作副本 HEAD 为 `39bbe0b`，与 Ubuntu 已合并历史不同，本轮未重置或覆盖双方 Git 历史。
- 工作区根目录的 09-09 提交清单与求助记录记载：fork 已推送、[PR #1](https://github.com/open-vela/contest2026_138_HomeMind/pull/1) 已创建，CLA/Actions 检查阻塞合入。本项仅承接已有记录，本轮没有实时查询 GitHub，不能据此断言检查目前仍未变化。

## 实测

诊断期间用 systemd 正常停止串口网关，结束时在 finally 路径恢复；未使用 `fuser -k` 杀进程。

| 阶段 | 观察 |
| --- | --- |
| 初始串口 | `heap_info` 正常，arena=8525536，free=6691872；`net_status` 显示 connected=yes、IP=`192.168.31.252` |
| 紧邻测试前网络基线 | ping 3 发 3 收，0% 丢包，平均 6.237 ms |
| 摄像头入口 | `media_probe` 输出 `MEDIA_PROBE_BEGIN video=/dev/video0 audio=/dev/audio/pcm_in0` 后，13 秒观察期内无后续输出 |
| 串口存活检查 | 随后发 `heap_info`，3 秒观察期内无响应 |
| 同期网络对照 | ping 20 发 0 收，100% 丢包，含 Ubuntu 返回的 Destination Host Unreachable |
| 恢复 | esptool 硬复位退出码 0；串口重新响应，free=6873848；初始联网仍在进行 |
| 恢复后复核 | 网关 `active`；稍后板子 ping 3 发 3 收，0% 丢包，平均 21.537 ms |

### 第二阶段：细粒度入口标记（2026-09-09）

恢复 I2C 核心文件备份后重新构建并烧录诊断镜像；因 watchdog 复位导致串口枚举为 `/dev/ttyACM1`，网关配置同步切换到 ACM1。探针首次基线为 2/3 收到（33.3% 丢包），脚本按返回码判定可继续。

| 阶段 | 观察 |
| --- | --- |
| 进入会话 | `nsh>` → `vela>`，`net_status` 显示 connected=yes、IP=`192.168.31.252` |
| 摄像头入口 | `MEDIA_PROBE_BEGIN` 后输出 `MEDIA_VIDEO_STAGE open_enter`、`[CAM-OPEN] mutex_enter` |
| 精确卡点 | 观察期内未出现 `[CAM-OPEN] mutex_locked`；因此当前卡在 `capture_open()` 获取 `cmng->mutex` 的等待 |
| 串口/网络存活 | 后续 `heap_info` 无响应；同期 ping 24 发 0 收，100% 丢包 |
| 恢复 | 探针退出码 0，网关恢复 `active`；未再次执行低层 I2C 标记烧录 |

## 结论与下一步

本轮补齐了 09-08 记录缺失的有效网络基线：不能再把这次现象只解释为 USB-CDC 控制台失灵，摄像头命令执行后网络响应也同时消失。

第二阶段已把范围收窄到 `capture_open()` 的 `cmng->mutex` 获取：看到 `mutex_enter`，没有看到 `mutex_locked`。这证明当前调用在进入 open 后等待管理锁，但还不能确定是哪一个线程持有锁、是否存在重复注册或未释放路径；也不能仅凭 ping 断定整机硬锁死。下一步应读取/标记锁持有者和 `capture_register` 生命周期，再验证 open/close 配对。

Ubuntu 当前驱动入口实际为 `nuttx/drivers/video/v4l2_cap.c:3610` 的 `capture_open`，内部依次执行 mutex、`IMGSENSOR_INIT`、`IMGDATA_INIT`、`initialize_resources`。板级传感器初始化为 `firmware/nuttx_media/esp32s3_board_camera.c:400` 的 `ov2640_init`。如 open 前后标记确认停在 open 内，再拆分这些阶段；不要沿不存在的 `videodev.c` 路径排查。

此次没有执行公网视觉命令，没有上传原始音视频。已完成一次包含诊断标记的构建与烧录；没有推送 Git 或合入 PR。

### 第三阶段：正式流程复测与有界锁实验回退（2026-09-09）

- 使用 `scripts/build.sh build` 完成正式构建，随后刷写并校验 `d2ef3ee790f2036a4aa6745f9e3fe2a0647f869d5a530c8d3f89da3c8caa6316`；刷写日志为 `/home/hfy/work/flash_formal_clean_20260909.log`，报告 `Hash of data verified`。
- 干净启动进入 `vela>`，`net_status` 为 `192.168.31.252`，摄像头命令前 ping 3/3；最新正式日志在 `open_enter` 后的 `[CAM-OPEN]` 行被串口交错截断，完整阶段复测仍是 `[CAM-OPEN] mutex_enter` 且未出现 `mutex_locked`，随后 heap/网络无响应。
- 临时 `timedlock`/`trylock` 改法没有形成可靠的 open 返回或可复核修复，已从默认部署路径和远端临时文件回退；当前源码恢复原始 `nxmutex_lock`，只保留阶段标记。该实验不能作为摄像头冻结修复结论。
- 本阶段仍未取得真实帧、TFLM 人员检测或端侧存在事件；下一步应定位锁持有者和注册/关闭生命周期。

## 本地复用工具

- 工作区私有脚本：`transfer/resume_camera_probe_20260909.py`，SSH 密码仅从环境变量读取。
- 第二阶段脚本：`transfer/stage_camera_probe_retry_20260909.py`；原始输出：`transfer/stage_camera_probe_retry_20260909.log`。
- 脱敏原始输出：`2026-09-09_camera_network_probe.log`（本目录）。
- 脚本要求命令返回预期板子 IP 且测试前 ping 成功后才启动对照；串口写入有超时；失响应后尝试硬复位并恢复网关。
