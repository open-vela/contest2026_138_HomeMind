# HomeMind 开发环境指南

> **历史文档：** 本文说明早期 Windows-first 环境。当前原生 Ubuntu 工作流请阅读 [迁移指南](../MIGRATION_UBUNTU.md)。

## 1. 为什么仍需要 Ubuntu

大赛规则没有要求必须使用虚拟机，但当前比赛分支的 OpenVela 快速入门以 Ubuntu 22.04 为支持环境，并明确不支持 WSL 或 Docker 编译。

本项目的 ESP32-S3 Xtensa 预编译工具链、OpenVela Shell 脚本、manifest 链接和 ai_agent 兼容补丁也以 Linux 环境为主。因此 Ubuntu 只作为构建基础设施，不作为日常桌面环境。

## 2. 推荐结构

```text
Windows 11
├── 本地比赛仓：server、miniprogram、docs、tests、tools
├── VS Code / Codex / 微信开发者工具
├── Python / FastAPI / SQLite
├── Mosquitto / MQTTX / 模拟设备
├── esptool / 串口工具
└── SSH
    └── Ubuntu 22.04 VM
        └── ~/work/openvela/
            ├── nuttx/
            ├── packages/
            ├── vendor/
            └── contest2026_138_HomeMind/
```

## 3. 虚拟机选择

VMware Workstation 和 VirtualBox 均可。项目不依赖某个特定产品，选择在当前电脑上稳定的即可。

建议配置：

- 4 个以上 CPU 核心
- 8–16 GB 内存
- 100 GB 以上动态磁盘
- NAT 网络
- OpenSSH Server
- USB 3.0（只在 Ubuntu 直接烧录时使用）

若电脑总内存只有 16 GB，优先给虚拟机 8 GB，并减少并行编译数量；不要为了达到建议值导致 Windows 频繁换页。

## 4. 文件系统规则

- OpenVela 全量工作区保存在 Ubuntu 的 ext4 文件系统中。
- 不在 VMware Shared Folders、VirtualBox Shared Folders 或 Windows NTFS 挂载目录中编译。
- 不使用 WSL 作为 OpenVela 构建环境。
- Windows 和 Ubuntu 通过 Git 同步代码。
- 使用 LF 作为 Shell、C、Python 和配置文件的仓库换行符。
- Shell 脚本必须保留可执行权限。

这些规则可以避免软链接、执行权限、路径格式、大小写和 CRLF 问题。

## 5. 日常开发流程

### 5.1 后端

在 Windows 使用独立 Python 虚拟环境：

```powershell
cd contest2026_138_HomeMind\server
python -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install -r requirements.txt
```

当前后端仍缺少 `services` 包，修复前主程序无法启动。完成修复后应使用：

```powershell
python -m uvicorn api.main:app --host 127.0.0.1 --port 8000 --reload
```

MiMo Key 通过环境变量设置，不写入 JSON、Compose 文件或源码。

### 5.2 MQTT

开发阶段在 Windows 启动本地 Mosquitto，用 MQTTX 观察消息。第一版使用模拟灯具验证控制闭环，待协议稳定后再连接真实硬件。

MQTT Topic 和消息格式需要单独形成协议文档，至少包含：

- Topic 命名
- 命令与状态消息 JSON Schema
- QoS 与 retained 策略
- 请求 ID、时间戳和错误码
- 设备离线和超时行为

### 5.3 小程序

在 Windows 微信开发者工具中打开 `miniprogram/`。开发阶段服务地址应配置在本地设置中，不把个人局域网地址固化到发布配置。

小程序必须逐步从本地存储迁移到 FastAPI 真实接口；本地存储只能用作缓存和离线演示。

### 5.4 固件

从 Windows VS Code 通过 Remote SSH 打开 Ubuntu 中的比赛仓。编译命令始终从 OpenVela 工作区根目录执行，而不是从比赛仓子目录执行。

固件开发顺序：

1. 官方 ai_agent 基线。
2. HomeMind 最小应用入口。
3. 一个可调用工具。
4. MQTT 控制闭环。
5. 主动安全事件。
6. 视觉、唤醒词等增强能力。

## 6. Windows 远程构建目标

计划中的 `tools/remote-build.ps1` 应只做可重复的确定性操作：

1. 检查本地是否存在未提交的固件改动。
2. 通过 SSH 更新 Ubuntu 中的目标分支。
3. 在 Ubuntu OpenVela 根目录执行构建。
4. 保存完整构建日志。
5. 通过 SCP 取回 `nuttx.bin` 和构建元数据。
6. 可选：用户明确指定 `COMx` 后执行 Windows 烧录。

脚本不得包含明文密码、API Key 或固定个人目录。

## 7. USB 与烧录策略

### 首次验证

使用虚拟机 USB 直通，按官方命令在 Ubuntu 烧录，先证明编译产物有效。

### 稳定开发阶段

将 USB 设备留给 Windows：

- Ubuntu 负责构建。
- Windows 取回固件。
- Windows `esptool` 烧录。
- Windows 串口工具采集日志。

这能减少 USB 在宿主机与虚拟机之间反复切换的问题。

## 8. 常见问题

### 编译缓慢

- 确保虚拟磁盘位于 SSD。
- 根据主机内存降低 `repo sync` 和编译并行度。
- 不从共享目录编译。
- 保留已验证的干净基线，避免无必要的 `distclean`。

### SSH 无法连接

- 检查 Ubuntu `systemctl status ssh`。
- 确认虚拟机 IP 和 NAT/桥接网络。
- 检查 Windows 防火墙及虚拟机网络配置。

### Ubuntu 看不到开发板

- 只在首次 Ubuntu 烧录时启用 USB 直通。
- 确认使用数据线，检查 `/dev/ttyACM*`。
- 若 Windows 正占用串口，先关闭对应串口程序。

### Windows 烧录失败

- 确认 COM 端口和下载模式。
- 关闭占用串口的其他程序。
- 使用与已验证 ESP-IDF 环境兼容的 `esptool` 版本。
- 先降低波特率排除链路稳定性问题。

## 9. 官方参考

- [OpenVela Ubuntu 快速入门](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/quickstart/openvela_ubuntu_quick_start.md)
- [ai_agent ESP32-S3 EYE 集成说明](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/ai_hardware/ai_agent_quickstart.md)
- [ESP-IDF ESP32-S3 入门](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/get-started/index.html)
