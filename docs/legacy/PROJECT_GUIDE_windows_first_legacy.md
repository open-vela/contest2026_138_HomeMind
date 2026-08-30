# HomeMind 项目指南

> 最后更新：2026-07-22

## 1. 项目概述

- **项目名称**：HomeMind：端云协同的家庭感知中枢
- **参赛方向**：AI 硬件产品创新
- **目标开发板**：ESP32-S3 EYE
- **核心平台**：OpenVela + ai_agent
- **专属仓库**：<https://github.com/open-vela/contest2026_138_HomeMind>
- **目标分支**：`dev-ai-contest-2026`
- **提交截止时间**：2026-09-20

HomeMind 使用 ESP32-S3 EYE 采集家庭事件，通过 OpenVela/ai_agent 进行理解和工具调用，由 Windows 上运行的私有云服务完成设备管理、事件存储和 MQTT 联动，并通过微信小程序呈现状态。

完整的开发环境分工、里程碑和时间计划见 [Windows-first 开发方案](docs/windows_first_development_plan.md)。

## 2. MVP 场景

### 2.1 自然语言设备控制

用户向 ai_agent 发出家庭设备控制指令，HomeMind 调用工具并通过 MQTT 控制模拟或真实设备，FastAPI 和小程序同步显示结果。

### 2.2 主动家庭安全提醒

ESP32-S3 EYE 产生摄像头、按键或模拟传感事件，HomeMind 主动创建告警，经 FastAPI 保存后在小程序展示。

## 3. 项目结构

```text
contest2026_138_HomeMind/
├── app/homemind/          # OpenVela 端应用、配置和 ai_agent Skills
├── board/                 # 比赛仓板级扩展示例
├── miniprogram/           # 微信小程序
├── server/                # FastAPI、MQTT 和持久化服务
├── docs/                  # 架构、环境和项目方案
├── scripts/               # Ubuntu 构建与环境脚本
├── tools/                 # Windows 远程构建和辅助工具（待实现）
├── tests/                 # 自动测试（待补齐）
└── logs/                  # 需要主动导出的 AI Coding 日志
```

组委会 manifest 会把比赛仓中的指定目录映射到完整 OpenVela 编译树。完整固件必须在比赛 manifest 同步后的 OpenVela 工作区根目录构建。

## 4. 开发环境原则

- 日常主环境是 Windows。
- FastAPI、MQTT、小程序、Git、文档和接口测试在 Windows 完成。
- OpenVela 固件编译和模拟器使用 Ubuntu 22.04 虚拟机。
- Windows 使用 VS Code Remote SSH 或 PowerShell/SSH 操作虚拟机。
- 不使用 WSL 或 Docker 编译 OpenVela。
- 不从 Windows 共享目录直接构建 OpenVela 全量工程。

开始搭建请阅读 [QUICKSTART.md](QUICKSTART.md) 和 [开发环境指南](docs/development_guide.md)。

## 5. 优先级

### P0：提交必需

- ESP32-S3 EYE 上运行官方 ai_agent 基线
- Wi-Fi、MiMo 和基础对话
- FastAPI、SQLite、MQTT 最小服务
- `device_control` 与 `home_security` 两个可执行 Skill
- 设备控制闭环和主动告警闭环
- 小程序真实 API 展示
- 可复现的构建、烧录和演示文档

### P1：核心增强

- 日程与主动提醒
- 隐私模式
- 断网缓存与恢复
- Windows 一键远程编译和烧录

### P2：风险较高的加分项

- 端侧视觉模型
- 离线唤醒词
- 手势识别
- 真实硬件或米家桥接

不得让 P2 功能阻塞 P0 闭环。

## 6. 开发规范

### 6.1 代码

- C 代码遵循 NuttX/OpenVela 现有风格。
- Python 遵循 PEP 8，并为 API 添加测试。
- 小程序遵循微信小程序规范。
- 配置文件中只保留示例值，不提交任何真实密钥。

### 6.2 Git

- 功能开发使用 `feature/<name>` 分支。
- 每个 PR 只解决一个明确问题。
- 推荐提交类型：`feat`、`fix`、`docs`、`test`、`refactor`、`chore`。
- 所有提交最终通过 PR 合入比赛专属仓。

### 6.3 完成定义

目录、接口声明、模拟数据或 Skill 文档不等于功能完成。只有实现、测试、目标环境验证和文档齐全后，才能在 [STATUS.md](STATUS.md) 中标记完成。

## 7. 硬件与范围约束

- ESP32-S3 EYE 是资源受限设备，不同时把人脸、手势、离线唤醒和所有通信通道作为第一阶段目标。
- 项目配置中的温湿度等数据需要外接传感器；没有外设时必须标记为模拟数据。
- 第一版米家控制使用标准 MQTT 模拟设备替代，真实米家桥接属于增强项。
- 先验证官方 ai_agent 能运行，再添加 HomeMind 自定义能力。

## 8. 文档导航

- [Windows-first 开发方案](docs/windows_first_development_plan.md)
- [快速开始](QUICKSTART.md)
- [开发环境指南](docs/development_guide.md)
- [当前状态](STATUS.md)
- [脚本说明](scripts/README.md)
- [AI Coding 日志说明](logs/README.md)
- [组委会仓库说明](README.md)

## 9. 官方资料

- [大赛总览](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/contest_overview.md)
- [参赛代码提交指南](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/code_submission_guide.md)
- [OpenVela Ubuntu 快速入门](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/quickstart/openvela_ubuntu_quick_start.md)
- [ai_agent 上手指南](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/ai_hardware/ai_agent_quickstart.md)

