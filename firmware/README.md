# HomeMind 固件源码收口

本目录保存 2026-08-30 已通过 ESP32-S3-EYE 真机验收的固件源码，不再依赖散落在 Ubuntu 生产工作区中的未跟踪修改。

## 内容

- `ai_agent_overlay/`：覆盖到 OpenVela `packages/ai_agent/` 的 HomeMind 源文件；
- `ai_agent_overlay/SOURCE_SNAPSHOT.json`：现场文件的路径、大小和 SHA-256；
- `patches/0001-homemind-esp32s3-nuttx.patch`：相对 NuttX 基线 `dd92bcf425738734d1b8aed09c2bd4dbe3f2e438` 的六个源文件补丁。

NuttX 补丁包含 ESP32-S3 Wi-Fi 内存回退池、WLAN/USB 串口稳定性、LittleFS `/data` 挂载、ESP32-S3-EYE 启动和 TCP 发送缓冲相关修改。补丁生成后已在由基线 commit 导出的临时文件树上通过 `patch -p1 --dry-run`。

## 使用

在由比赛 manifest 同步的完整 OpenVela 工作区中执行：

```bash
cd contest2026_138_HomeMind
OPENVELA_ROOT=$HOME/work/openvela ./scripts/build.sh deploy
OPENVELA_ROOT=$HOME/work/openvela JOBS=2 ./scripts/build.sh build
```

`tools/deploy-to-vm.sh` 会复制全部 ai_agent overlay，并以幂等方式应用 NuttX 补丁。若目标源码既不是记录的基线，也不是已应用补丁状态，脚本会中止，不会强行覆盖未知 NuttX 修改。

## 边界

该收口解决“源码未进入团队仓”的问题，但仍须在新建的干净 OpenVela 工作区执行一次从 manifest 同步到 BIN 的完整复现，才能标记为最终可复现基线。旧 Ubuntu 工作区 Git 元数据异常，仅作为历史验收现场保留，不执行 `reset`、`clean` 或强制同步。
