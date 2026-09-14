# HomeMind：端云协同的家庭感知中枢

HomeMind 是运行在 ESP32-S3-EYE 上的家庭智能终端。项目以 OpenVela/Apache NuttX 和 `ai_agent` 为端侧基础，通过局域网家庭服务、MQTT、MiMo 和微信小程序形成“感知—规划—执行—反馈”闭环。

> 当前状态（更新 2026-09-07，事实截止 2026-09-06）：**部分完成**。联网、MiMo、LED/LCD、持久化、Skill 延时执行、米家真实灯光及模拟器实体同步已有记录；严格私有化与离线感知仍待开发。视觉现场 BIN `a0029225…` 与本地 `8cf605d9…` 不一致，源码与产物待归集。唯一当前摘要与版本边界见 [STATUS](STATUS.md)。

## 参赛方向与硬件

- 赛道：AI 硬件产品创新；
- 主控：ESP32-S3-EYE；
- 系统：OpenVela / Apache NuttX；
- 已使用外设：板载 LED、BOOT 键、ST7789 LCD；
- 板载外设：OV2640 摄像头、板载 I2S 数字麦克风；NuttX 板级适配已构建、烧录并完成两次原始帧/PCM 探针验收；尚无端侧视觉推理或连续音频流验收；
- 板上没有扬声器，比赛版使用 LCD/LED 和小程序反馈，不承诺语音播报；
- 不使用 ESPHome，不制作外壳或自制 PCB。

## 已通过真机验证的能力

- ESP32-S3-EYE 构建、烧录和进入 `ai_agent`；
- Wi-Fi 凭据与 MiMo 配置保存在 LittleFS `/data`；
- 冷启动自动联网 5/5、强制断网恢复 3/3；
- HTTPS/TLS 访问 MiMo，并取得非固定的真实模型回复；
- 模型调用白名单工具控制板载 LED、读取设备信息和 BOOT 键；
- 240×240 LCD 显示 Wi-Fi、MiMo 配置和 LED 状态；
- 已验证固件产物及 SHA-256 位于 `artifacts/`。

详细证据和历史问题见 [STATUS.md](STATUS.md)；比赛后续路线见 [HomeMind 后续路线与评估](docs/HomeMind_后续路线与评估_2026-09-06.md)。这些记录包含不同日期的阶段状态，最终作品说明只采用带最新日期的验收结论。

## 比赛截止前的交付边界

| 能力 | 状态与剩余工作 |
| --- | --- |
| 连续音频与离线语音 | 部分完成：仅有限 640 字节 PCM；指定词“你好，openvela”与一个本地 LED 语音命令未实现 |
| 端侧有人/无人 | 未实现：已有真实取帧，TFLM INT8 模型与存在事件待接入 |
| 家庭私有服务与语义编排 | 未实现：家庭部署、转写、必要文本 MiMo 和受校验计划待打通 |
| 米家与小程序 | 部分完成：真实灯光、模拟器控制与实体同步有证据；当前多功能房吸顶灯手机全链路、弱网和权限待验证 |
| 日程、事件、资产 | 部分完成：日程仅在小程序本机；家庭 SQLite 模型与跨端同步未实现 |
| 最终版本与材料 | 部分完成：最新源码/产物归集、完整回归、真实日志校验及远端合入待完成 |

排期和验收条件见[比赛冲刺计划](docs/HomeMind_比赛冲刺计划_2026-08-30_至_2026-09-20.md)。身份识别、手势、机器人移动与导航留在赛后，小爱音箱播报不是必交能力。

## 目标架构与隐私边界（待开发验收）

```text
OpenVela / ai_agent：摄像头端侧检测、I2S 连续音频、关键词与本地 LED
    ↕ 局域网 MQTT / 必要音频
家庭主机：FastAPI + SQLite + Mosquitto + 转写 + 白名单任务编排
    ├─ 必要文本 ↔ MiMo
    ├─ 家庭网关 → Home Assistant → 多功能房吸顶灯
    └─ 主动出站连接 ↔ 腾讯云远程入口 ↔ 微信小程序
```

正式隐私模式要求原始音视频留在板端或家庭局域网，家庭主机存储事件、待办/日程和资产；日程复用待办到期与提醒字段。仅必要文本允许发送 MiMo，不发送完整日程或资产记录。腾讯云不持久化业务正文，但远程小程序数据仍经过公网入口。

当前公网视觉实验上传原始图像至腾讯云转码再送 MiMo；它是历史实验成果，正式隐私模式须关闭该路径并核查出站请求/日志。严格私有化与离线感知仍未完成，文档修改不构成实现证据。

## 目录

| 路径 | 用途 |
| --- | --- |
| `firmware/ai_agent_overlay/` | 本地 ai_agent 快照；最新视觉现场源码待归集 |
| `firmware/patches/` | 相对记录基线的 NuttX 最小补丁 |
| `scripts/build.sh` | Linux 环境检查、源码部署、构建和烧录 |
| `tools/deploy-to-vm.sh` | 幂等安装 ai_agent overlay 和 NuttX 补丁 |
| `backend/` | FastAPI、MQTT、WSS、SQLite 和家庭网关 |
| `miniprogram/` | 微信小程序 |
| `app/homemind/skills/` | 已验收的运行时 Skill 原文及静态校验器 |
| `artifacts/` | 本地 BIN/ELF 及 SHA-256；与视觉现场版本有差异 |
| `logs/` | 按官方采集器导出的 AI Coding 日志 |
| `docs/submission/` | 官方作品模板和提交合规清单 |

模板目录 `app/hello_app/`、`quickapp/`、`board/contest_board/` 不是 HomeMind 核心功能。

## 构建与烧录

先按比赛 manifest 同步完整工作区：

```bash
repo init -u https://github.com/open-vela/contest2026_138_HomeMind \
  -b dev-ai-contest-2026 -m contest2026_138_HomeMind.xml
repo sync -c -j4
```

在团队仓执行：

```bash
cd contest2026_138_HomeMind
./scripts/setup_ubuntu.sh
OPENVELA_ROOT=$HOME/work/openvela ./tools/verify-ubuntu.sh
OPENVELA_ROOT=$HOME/work/openvela ./scripts/build.sh deploy
OPENVELA_ROOT=$HOME/work/openvela JOBS=2 ./scripts/build.sh build
SERIAL_PORT=/dev/ttyACM0 OPENVELA_ROOT=$HOME/work/openvela ./scripts/build.sh flash
```

`firmware/` 的来源和补丁基线见 [固件源码收口说明](firmware/README.md)。最终提交前还需在新建的干净 OpenVela 工作区完成一次完整复现。

## 安全边界

- Wi-Fi 密码、MiMo Token、服务器密码、AppSecret、私钥不进入仓库；
- LLM 只能调用带 schema 的白名单工具，不具有任意 shell 权限；
- MQTT 命令需要设备映射、TTL、幂等键和结果状态；
- 小程序不保存 MiMo Token、设备主密钥或 AppSecret；
- 未接入传感器显示“未接入/--”，服务失败显示失败，不返回假成功；
- 泄露过的历史凭据必须轮换后才能进入最终演示。

## 已知限制

- 现场验收、本地代码与官方远端必须区分：截至 09-06 远端仍为模板 `961cf680…`，最新视觉版本待归集；本地已有构建记录不等于最终干净复现通过。
- 网关 9/9 是用户本轮核查的软件测试结果，不替代硬件验收。
- 手机真机、连续采样、离线语音、端侧视觉、家庭服务迁移与业务同步仍按 STATUS 保留缺口。
- 真实 AI 日志仍需按参赛者 GitHub 登录名完成官方采集和校验；报告、照片、视频及提交回执待完成。

## 提交要求

作品以官方团队仓 `dev-ai-contest-2026` 分支为评审事实源。最终提交前必须确认全部 PR 已合并、真实 AI 日志已校验、官方 DOCX/PDF 与不超过 5 分钟的演示视频齐全。参见 [提交合规清单](docs/submission/HomeMind_作品提交合规清单.md)。
