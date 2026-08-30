# HomeMind 快速开始

> **历史文档：** 本页记录早期 Windows-first 工作流。当前请使用原生 Ubuntu，并从 [MIGRATION_UBUNTU.md](MIGRATION_UBUNTU.md) 开始。

## 1. Windows 端准备

安装并验证以下工具：

- Git for Windows
- VS Code，建议安装 Remote - SSH 扩展
- Python 3.11
- 微信开发者工具
- Mosquitto 和 MQTTX（端云联调阶段使用）
- VMware Workstation 或 VirtualBox

在 PowerShell 中验证：

```powershell
git --version
python --version
ssh -V
```

FastAPI 后端最终使用独立虚拟环境。当前服务仍有缺失模块，修复前不要把“依赖安装成功”视为服务已可运行。

## 2. 建立 Ubuntu 22.04 编译节点

创建 Ubuntu 22.04 x86_64 虚拟机：

- CPU：4 核以上
- 内存：8 GB 起，宿主资源允许时使用 12–16 GB
- 磁盘：至少 100 GB，使用 SSD
- 网络：NAT
- USB：USB 3.0；仅首次验证 Ubuntu 烧录时需要直通

在 Ubuntu 中安装 OpenSSH 和基础依赖：

```bash
sudo apt update
sudo apt install -y openssh-server git curl cmake python3 libc++abi-dev build-essential git-lfs
sudo systemctl enable --now ssh
git lfs install
hostname -I
```

然后从 Windows 测试连接：

```powershell
ssh <ubuntu-user>@<ubuntu-ip>
```

## 3. 同步完整 OpenVela 工程

完整工程必须在 Ubuntu 的 Linux 文件系统中同步。不要把工作区放到 Windows 共享目录。

先按官方文档安装 `repo` 和 ESP-IDF，再执行比赛仓提供的 manifest：

```bash
mkdir -p ~/work/openvela
cd ~/work/openvela

repo init -u https://github.com/open-vela/contest2026_138_HomeMind \
  -b dev-ai-contest-2026 \
  -m contest2026_138_HomeMind.xml \
  --git-lfs
repo sync -c -j8
```

同步后，比赛仓位于：

```text
~/work/openvela/contest2026_138_HomeMind/
```

OpenVela 全量源码位于其上一级 `~/work/openvela/`。

## 4. 编译官方 ESP32-S3 EYE 基线

在 OpenVela 工作区根目录执行官方流程：

```bash
cd ~/work/openvela

cp packages/ai_agent/defconfigs/esp32s3-eye/esp32s3-eye_defconfig \
  nuttx/boards/xtensa/esp32s3/esp32s3-eye/configs/ai_agent/defconfig

source /path/to/esp-idf/export.sh
export CCACHE_DISABLE=1

./build.sh esp32s3-eye:ai_agent distclean
bash packages/ai_agent/fix_esp32s3.sh &
./build.sh esp32s3-eye:ai_agent
```

第一目标是先成功构建官方未修改基线，再添加 HomeMind 代码。迁移完成后可使用已修复的 `scripts/build.sh` 作为检查、部署、构建和烧录入口。

## 5. 烧录与基础验证

首次可以按官方方法在 Ubuntu 烧录：

```bash
esptool.py -c esp32s3 -p /dev/ttyACM0 -b 460800 \
  --before default_reset --after hard_reset \
  write_flash 0x0 nuttx/nuttx.bin
```

后续目标是在 Windows 安装匹配版本的 `esptool`，把固件取回 Windows 后通过 `COMx` 烧录。迁移之前必须先确认固件路径、工具版本和烧录参数。

板上验证顺序：

```text
nsh> ai_agent
vela> set_wifi <ssid> <password>
vela> net_test
vela> router_set mimo <api_key>
vela> router_model 0 mimo-v2.5
vela> ask 你好，请介绍一下你自己
```

不要把真实 SSID、密码或 API Key 写入仓库。

## 6. Windows 日常工作流

### 服务端与小程序

直接在 Windows 本地仓开发：

- `server/`
- `miniprogram/`
- `docs/`
- `tests/`
- `tools/`

### 固件

优先从 Windows VS Code 使用 Remote SSH 打开 Ubuntu 中的比赛仓：

```text
~/work/openvela/contest2026_138_HomeMind/
```

这样编辑器仍运行在 Windows，但源码、Shell 工具和编译过程位于 Ubuntu。

### Git 同步

- 不在两端同时修改同一文件。
- 使用 `feature/<name>` 分支。
- 小步提交并通过 GitHub 在 Windows 与 Ubuntu 间同步。
- 不手工复制整个 OpenVela 工作区。

## 7. 下一步

完成本指南后，按以下顺序推进：

1. 修复仓库现有脚本和应用骨架。
2. 完成 [STATUS.md](STATUS.md) 中的 B0。
3. 在 Windows 跑通 FastAPI 和小程序。
4. 建立 MQTT 设备控制闭环。
5. 建立主动安全告警闭环。

完整计划见 [Windows-first 开发方案](docs/windows_first_development_plan.md)。

## 8. 官方参考

- [OpenVela Ubuntu 快速入门](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/quickstart/openvela_ubuntu_quick_start.md)
- [ai_agent 应用开发上手指南](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/ai_hardware/ai_agent_quickstart.md)
