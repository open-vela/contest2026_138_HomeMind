# HomeMind 工程指南

## 1. 产品主线

HomeMind 当前只推进一条主线：ESP32-S3-EYE 在 OpenVela/ai_agent 上自动连接 Wi-Fi，直接调用 MiMo，再执行经过白名单校验的本地工具。

第一版验收链路：

```text
上电 → 自动联网 → 获得 IPv4 → HTTPS/MiMo 文本问答
    → light_set 工具 → LED 状态改变 → 掉电后配置仍保留
```

不把 Windows/Linux 网关、FastAPI、MQTT Broker、代理、小程序、语音、视觉和 STM32 作为当前链路的前置条件。

## 2. 双机职责

### Ubuntu 22.04

- 完整 OpenVela `repo` 工作区；
- `packages/ai_agent` 和 NuttX 调试；
- HomeMind staging 源码；
- 固件编译、镜像生成、烧录；
- 串口日志、网络诊断和实机验收；
- 固件分支的 Git 提交。

### Win11

- `miniprogram/` 微信小程序；
- 工作区上级 `外壳/` 的 UG 与 STL；
- 两类 Windows 资产的备份；
- 不再承担 OpenVela 编译。

## 3. 目录状态

```text
contest2026_138_HomeMind/
├── app/homemind/        设计配置和 Skill 草案
├── artifacts/           基线 BIN/ELF 与哈希
├── docs/                当前文档
│   └── legacy/          旧比赛模板和历史入口
├── logs/                比赛 AI Coding 日志位置
├── miniprogram/          Win11 专属开发区
├── scripts/              Ubuntu 安装和构建入口
├── tools/                固件 staging、迁移、烧录和测试资料
├── server/               冻结的旧端云方案
├── app/hello_app/        组委会样例
├── quickapp/             组委会样例
└── board/contest_board/  组委会占位板级样例
```

## 4. 当前固件源码入口

当前真正参与 HomeMind 网络调试的是：

- `tools/staging-network_manager.c`；
- `tools/staging-vela_tls.c`；
- `tools/staging-http_proxy.c`；
- `tools/deploy-to-vm.sh`；
- `scripts/build.sh`。

`staging-homemind_tools.c` 尚未进入部署和工具注册流程，不能当作已完成功能。

## 5. 开发顺序

1. 在 Ubuntu 恢复有效 Git 和官方 OpenVela revision；
2. 编译、烧录官方 ai_agent 基线；
3. 编译包含脱敏日志的 HomeMind 修改版；
4. 只排查 Wi-Fi 关联和 DHCP，稳定获得 IPv4；
5. 再排查 DNS/TCP/TLS；
6. 接入真实 MiMo 文本问答；
7. 实现掉电持久化；
8. 注册并验证 `device_status`、`light_set`、`scene_set`；
9. 最后整理小程序展示、外壳和比赛提交材料。

## 6. 完成定义

- 文件或接口存在：只算草案；
- ELF 中能检出：算已进入固件；
- 在目标板真实运行：才算已验证；
- 连续、重启和异常测试通过：才算阶段完成。

每次测试至少记录日期、源码 commit、固件哈希、命令、实际输出摘要和结论。日志必须脱敏。

## 7. 入口文档

- [Ubuntu 迁移](MIGRATION_UBUNTU.md)
- [快速开始](QUICKSTART.md)
- [当前状态](STATUS.md)
- [双机开发工作流](docs/DEVELOPMENT_WORKFLOW.md)
- [串口手册](tools/serial-operations.md)
