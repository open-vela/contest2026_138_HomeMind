# 2026-09-09 摄像头诊断交接

状态：**暂停，交由 WORKBUDDY 继续**。本记录是本轮最后状态；不要把诊断镜像或阶段标记当作端侧人员检测验收。

## 环境与服务

- Windows 工作副本：`C:\Users\a2760\Desktop\HomeMind\contest2026_138_HomeMind_official`。
- Ubuntu 官方工作区：`/home/hfy/work/openvela-clean-20260830/contest2026_138_HomeMind`；Ubuntu 根目录：`/home/hfy/work/openvela-clean-20260830`；官方仓 HEAD 为 `aaefdcb`，工作区有本轮未提交改动。
- 交接前已确认 `homemind-api`、`homemind-gateway`、`homemind-relay` 均为 `active`；`/dev/homemind-esp32` 当前解析到 `/dev/ttyACM0`。后续串口操作必须优先使用稳定别名，并在独占探针结束后恢复网关。
- API 只读健康检查：`curl http://127.0.0.1:8001/v1/health` 应返回 `{"status":"ok","service":"homemind-c1"}`。

## 最后正式构建与刷写

- 通过 Ubuntu 官方流程 `scripts/build.sh build` 完成构建，日志：`/home/hfy/work/build_final_formal_20260909.log`。
- 当前正式诊断产物位于官方仓 `artifacts/`：
  - `contest2026_138_HomeMind/artifacts/nuttx.bin`：`d2ef3ee790f2036a4aa6745f9e3fe2a0647f869d5a530c8d3f89da3c8caa6316`
  - `contest2026_138_HomeMind/artifacts/nuttx.elf`：`8b87542fcd14961e95a81ea30630c5cfaeb72b05718b2c7ed023047f3f5028e4`
- 刷写日志：`/home/hfy/work/flash_formal_clean_20260909.log`；esptool 输出 `Hash of data verified`，写入 1,420,036 字节后 hard reset。

## 最后探针事实

日志：`/home/hfy/work/probe_final_formal_20260909.log`；探针脚本：`/home/hfy/work/stage_camera_probe_bounded_lock_20260909.py`。

1. 干净启动可到 `vela>`；`net_status` 显示联网，IP 为 `192.168.31.252`。
2. `media_probe` 前基线 ping 为 3/3 收到、0% 丢包。
3. 摄像头入口输出 `MEDIA_PROBE_BEGIN video=/dev/video0 audio=/dev/audio/pcm_in0`、`MEDIA_VIDEO_STAGE open_enter`；最新正式日志的串口交错把下一行截断为 `[CAM-OPEN] m...`。同一阶段标记的完整复测记录已看到 `[CAM-OPEN] mutex_enter`，观察期内没有 `[CAM-OPEN] mutex_locked`。
4. 随后 `heap_info` 无响应，同期板端 ping 丢失；这把范围收窄到 `capture_open()` 等待 `cmng->mutex`，但尚未证明具体持有者、重复注册路径或整机硬锁死。
5. 本轮未执行公网视觉命令，也没有上传原始音视频；没有 TFLM 人员检测、真实帧推理或隐私模式验收结果。

## 已回退的实验

- 曾试验 `nxmutex_timedlock`/`nxmutex_trylock` 的临时有界锁改法。它没有形成可靠的 open 返回和可复核修复，且个别复位/USB 重枚举使结果不稳定。
- 临时补丁已从本地与 Ubuntu 默认部署路径移除；Ubuntu `nuttx/drivers/video/v4l2_cap.c` 已恢复原始阻塞 `nxmutex_lock(&cmng->mutex)`，仅保留 `[CAM-OPEN]` 阶段标记。不要把有界锁实验重新作为默认修复。
- 板级源中的静默启动改动保留：`board_camera_initialize()` 调用 `ov2640_start_xclk(0)`，运行时 `ov2640_init()` 才启用详细 XCLK 标记，避免启动控制台被早期日志干扰。

## WORKBUDDY 下一步

1. 先复核服务、稳定串口别名、远端工作区 dirty 状态及上述 BIN/ELF 哈希；改动前备份远端 `nuttx/drivers/video/v4l2_cap.c`、板级摄像头源和 ai_agent overlay。
2. 在 `capture_register`/视频设备注册、初始化线程、`capture_open`/`capture_close` 配对处增加可复核的所有权和生命周期观测，优先回答“谁持有 `cmng->mutex`、何时释放、是否重复注册”。避免先加入 trylock/timedlock 规避现象。
3. 先让 `open → frame/明确错误 → close` 稳定且网络/串口保持可用，再恢复真实帧探针；随后才接 TFLM INT8 人员检测和存在事件。
4. 每次独占串口探针都应停止 `homemind-gateway`，使用 `/dev/homemind-esp32`，并在 `finally` 路径启动网关；失响应时先保存串口和 ping 日志，再做硬复位。
5. 诊断期间不执行公网原始图像外发，不做 Git push、PR 合入或云端发布；结论必须区分“源码/构建通过”和“实机推理验收”。

## 常用复核命令

```sh
systemctl is-active homemind-api homemind-gateway homemind-relay
readlink -f /dev/homemind-esp32
curl http://127.0.0.1:8001/v1/health
sed -n '3618,3630p' /home/hfy/work/openvela-clean-20260830/nuttx/drivers/video/v4l2_cap.c
sha256sum /home/hfy/work/openvela-clean-20260830/contest2026_138_HomeMind/artifacts/nuttx.bin \
  /home/hfy/work/openvela-clean-20260830/contest2026_138_HomeMind/artifacts/nuttx.elf
```

本轮未执行 push/merge；Ubuntu 官方工作区仍有未提交的诊断、模型初始化、板级摄像头、产物和文档改动，Windows 本地副本保留文档、板级源和 udev 规则改动。WORKBUDDY 接手时应先查看两处 `git status --short`，不要重置或覆盖这些改动。
