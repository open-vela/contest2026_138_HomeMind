# HomeMind Windows-first 开发方案

> **历史文档：** 该方案已被原生 Ubuntu 工作流取代。当前入口见 [MIGRATION_UBUNTU.md](../MIGRATION_UBUNTU.md)。

> 版本：2026-07-22  
> 目标：尽可能在 Windows 完成日常开发，只将 OpenVela 必需的编译与模拟器工作放在 Ubuntu 22.04。

## 1. 方案结论

HomeMind 采用“Windows 主开发机 + Ubuntu 22.04 编译节点”的混合工作流。

- Windows 承担 Codex、VS Code、Git、FastAPI、MQTT、小程序、接口测试、串口监视和尽可能多的固件烧录工作。
- Ubuntu 22.04 虚拟机仅承担 OpenVela 全量源码同步、ESP32-S3 固件编译、`menuconfig`、官方兼容补丁和 OpenVela 模拟器。
- Windows 通过 VS Code Remote SSH 或 PowerShell/SSH 操作 Ubuntu，虚拟机可长期后台运行，不要求日常进入 Ubuntu 桌面。
- OpenVela 全量工作区保存在 Ubuntu 的 Linux 文件系统中，不放在 VMware/VirtualBox 共享目录，也不从 Windows NTFS 目录直接编译。

大赛没有强制要求使用虚拟机；虚拟机只是 Windows 电脑运行受支持 Ubuntu 环境的一种方式。当前比赛分支的官方快速入门以 Ubuntu 22.04 为支持环境，并明确不支持在 WSL 或 Docker 中编译 OpenVela。

## 2. 项目目标与最小可交付版本

HomeMind 的最终定位是运行在 ESP32-S3 EYE 上的端云协同家庭感知中枢，核心技术栈为 OpenVela、ai_agent、FastAPI、MQTT 和微信小程序。

最小可交付版本（MVP）只要求完成两个闭环。

### 2.1 自然语言设备控制

```text
用户向 ai_agent 发出指令
  -> HomeMind Skill 选择设备控制工具
  -> MQTT 发布控制命令
  -> Windows 家庭服务/模拟设备执行
  -> FastAPI 保存设备状态
  -> 微信小程序展示结果
```

第一版使用 MQTT 模拟灯具即可。真实智能插座或米家桥接属于增强项，不得阻塞 MVP。

### 2.2 主动家庭安全提醒

```text
ESP32-S3 EYE 产生摄像头、按键或模拟传感事件
  -> HomeMind 进行事件判断
  -> ai_agent 主动任务生成告警
  -> FastAPI 接收并保存事件
  -> 微信小程序展示告警与可选快照
```

第一版先验证“主动产生事件并完成通知”，再决定是否加入端侧人脸检测或手势识别。

## 3. Windows 与 Ubuntu 职责

| 工作内容 | 主要环境 | 说明 |
| --- | --- | --- |
| Codex、VS Code、文档 | Windows | 日常主界面 |
| Git 分支与提交 | Windows | GitHub 是两侧同步边界 |
| FastAPI、SQLite、pytest | Windows | 使用原生 Python 虚拟环境 |
| Mosquitto、MQTTX、模拟设备 | Windows | 不依赖 Ubuntu |
| 微信开发者工具 | Windows | 小程序开发与调试 |
| MiMo API 调试 | Windows | Key 仅放环境变量 |
| OpenVela 全量源码与 `repo` | Ubuntu VM | 保存在 Linux 文件系统 |
| OpenVela/ESP32-S3 编译 | Ubuntu VM | 官方支持路径 |
| `menuconfig`、官方修复脚本 | Ubuntu VM | 构建必需 |
| QEMU/Vela Emulator | Ubuntu VM | 仅在需要时运行 |
| 固件烧录、串口日志 | Windows 优先 | 首次可在 Ubuntu 验证 |

## 4. 推荐开发拓扑

### 4.1 Windows 本地工具

- Git for Windows
- VS Code 和 Remote - SSH 扩展
- Python 3.11（项目使用独立 `.venv`）
- 微信开发者工具
- Mosquitto 或其他本地 MQTT Broker
- MQTTX
- `esptool` 和串口工具
- 可选：Docker Desktop，仅用于服务端容器；不要用于 OpenVela 编译

### 4.2 Ubuntu 编译节点

- Ubuntu 22.04 x86_64
- 建议 4 核以上、8–16 GB 内存、至少 100 GB 磁盘
- OpenSSH Server
- ESP-IDF 和 OpenVela 官方依赖
- 通过比赛 manifest 同步的完整 OpenVela 工作区

### 4.3 代码同步原则

1. Windows 本地主要开发 `server/`、`miniprogram/`、`docs/` 和工具脚本。
2. 固件代码通过 Windows VS Code Remote SSH 在 Ubuntu 工作区编辑，或先在 Windows 提交后由 Ubuntu 拉取。
3. 不同时在两端修改同一分支的同一文件。
4. OpenVela 工作区之间不使用手工复制作为长期同步方案。
5. 所有功能开发使用小型 feature 分支，通过 GitHub PR 合入比赛分支。

后续应增加 `tools/remote-build.ps1`，由 Windows 自动触发 Ubuntu 编译并取回 `nuttx.bin`。在确认官方烧录参数和 `esptool` 版本后，再加入 Windows 一键烧录。

## 5. 功能优先级

### P0：必须完成

- 官方 `esp32s3-eye:ai_agent` 固件可编译、烧录和启动
- Wi-Fi、MiMo Router 和基础 `ask` 对话可用
- FastAPI 健康检查、设备、事件接口可运行
- MQTT 设备控制闭环
- `device_control` 和 `home_security` 两个 Skill 真正注册并可执行
- 小程序展示真实设备状态和安全事件
- 一套可重复的构建、启动和演示说明

### P1：应当完成

- SQLite 持久化
- 日程和主动提醒
- 隐私模式与数据保留策略
- 断网缓存、恢复重传和错误状态展示
- Windows 远程编译、取回固件和烧录脚本

### P2：有余力再做

- 端侧轻量视觉模型
- 离线唤醒词
- 手势识别
- 真实 MQTT 设备
- 米家桥接
- 多种语音/消息通道

ESP32-S3 EYE 没有项目配置中声明的全部环境传感器。温湿度等功能必须连接外部传感器，否则应从 MVP 移除或清楚标记为模拟数据。

## 6. 阶段计划

### 2026-07-22 至 2026-07-25：建立可信基线

- 安装并验证 Windows 必需工具。
- 创建 Ubuntu 22.04 编译节点并同步完整 OpenVela 工程。
- 按官方流程编译一次未修改的 `esp32s3-eye:ai_agent`。
- 修复项目构建脚本中丢失的 Shell 变量。
- 修复小程序 JavaScript 语法错误。
- 补齐或简化 FastAPI 服务层，使 `/api/health` 可运行。
- 建立最小自动测试并修正项目状态文档。

验收标准：服务器能启动、小程序能加载、官方固件能生成。

### 2026-07-26 至 2026-07-31：开发板最小能力

- 固件启动进入 `nsh>`。
- 启动 ai_agent 并进入 `vela>`。
- 完成 Wi-Fi、MiMo 配置和第一次 `ask`。
- 确定 Windows 烧录和串口监视流程。
- 建立 Windows 到 Ubuntu 的 SSH 构建入口。

验收标准：不进入 Ubuntu 桌面也能发起构建，开发板能够完成基础对话。

### 2026-08-01 至 2026-08-10：端云控制闭环

- FastAPI 增加服务层和 SQLite。
- 确定 MQTT Topic、QoS 和 JSON 消息协议。
- 实现 Windows MQTT 模拟灯具。
- 实现 ai_agent 设备控制工具与 `device_control` Skill。
- 小程序读取 FastAPI 的真实设备状态。

验收标准：“打开客厅灯”能够从自然语言一直执行到设备状态反馈。

### 2026-08-11 至 2026-08-23：主动安全闭环

- 验证 ESP32-S3 EYE 摄像头采集。
- 先用按键、模拟事件或简单图像指标完成触发。
- 实现 `home_security` 工具和主动任务。
- 服务端保存安全事件，小程序展示告警。
- 实现隐私开关和最小数据上传策略。

验收标准：设备无需用户再次提问即可主动生成并展示一条告警。

### 2026-08-24 至 2026-09-06：增强与可靠性

- 按优先级选择日程、每日简报、真实 MQTT 设备或一个端侧视觉能力。
- 完成断网恢复、超时、重试和错误显示。
- 连续运行测试，记录内存、延迟和失败率。

### 2026-09-07 至 2026-09-13：演示与文档

- 冻结核心功能。
- 完成作品 README、架构图、部署说明和测试报告。
- 整理 AI Coding 日志。
- 准备一键启动服务和可复现的烧录流程。

### 2026-09-14 至 2026-09-19：提交缓冲

- 从全新目录完整复现一次。
- 录制不超过 5 分钟的演示视频并准备 PPT/PDF。
- 检查仓库秘密、许可证、PR、CLA 和最终交付物。
- 只修复阻塞问题，不再增加大功能。

## 7. 四个强制里程碑

1. **B0：官方 ai_agent 固件成功运行。**
2. **B1：Windows 后端与小程序使用真实 API 联通。**
3. **B2：自然语言通过 ai_agent 和 MQTT 执行设备控制。**
4. **B3：设备主动产生家庭安全事件并完成展示。**

B0 至 B3 全部完成后，项目才进入增强功能阶段。

## 8. 测试与完成定义

### Windows 快速检查

- 每次后端变更运行 Python 单元测试和 API 测试。
- 每次小程序变更至少通过开发者工具编译和关键页面手测。
- MQTT 协议变更使用模拟设备完成往返测试。
- 配置和日志中不得提交 API Key、Wi-Fi 密码或家庭地址。

### Ubuntu 集成检查

- 固件相关 feature 分支合入前必须完成一次干净构建。
- defconfig 或依赖变化后执行 clean build。
- 每周至少进行一次真实开发板冒烟测试。
- 发布候选版本必须记录固件哈希、构建命令和烧录命令。

### 功能完成定义

一个功能只有同时满足以下条件才标记为完成：

- 有可运行实现，而不是只有目录、接口或 Skill 文档。
- 有测试或明确的人工验证步骤。
- 在目标环境实际运行过。
- 文档与配置不包含秘密。
- `STATUS.md` 中记录验证结果和日期。

## 9. 官方参考

- [openvela Ubuntu 快速入门](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/quickstart/openvela_ubuntu_quick_start.md)
- [ai_agent 应用开发上手指南](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/ai_hardware/ai_agent_quickstart.md)
- [大赛总览](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/contest_overview.md)
