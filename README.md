# HomeMind：端云协同的家庭感知中枢

HomeMind 是运行在 ESP32-S3-EYE 上的家庭智能终端。项目以 OpenVela/Apache NuttX 和 `ai_agent` 为端侧基础，通过局域网家庭服务、MQTT、MiMo 和微信小程序形成“感知—规划—执行—反馈”闭环。

> 当前状态（2026-08-30）：OpenVela 固件、自动联网、MiMo 真实问答、白名单本地工具、Flash 持久化和 LCD 状态界面已经真机验证；摄像头端侧推理、离线唤醒、真实米家设备和最终小程序闭环尚未完成。未完成能力不会使用固定值或测试输入冒充。

## 参赛方向与硬件

- 赛道：AI 硬件产品创新；
- 主控：ESP32-S3-EYE；
- 系统：OpenVela / Apache NuttX；
- 已使用外设：板载 LED、BOOT 键、ST7789 LCD；
- 待接入外设：OV2640 摄像头、板载 I2S 数字麦克风；
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

详细证据和历史问题见 [STATUS.md](STATUS.md)。这些记录包含不同日期的阶段状态，最终作品说明只采用带最新日期的验收结论。

## 比赛截止前的交付边界

以下功能正在按冲刺计划实施，完成前均视为“未实现”：

1. “你好，openvela”离线唤醒，并通过 LCD/LED 反馈；
2. 板载摄像头实时人员/人脸存在检测；
3. 文本意图经家庭服务和 MiMo 转换为受 JSON Schema 约束的任务；
4. MQTT 命令的 `acked → done → status` 状态闭环；
5. 一台真实米家灯或插座的开关控制；
6. 小程序展示真实设备、事件、待办、日程和资产；
7. 一个安装到 `/data/agent/skills/`、由设备实际加载执行的自定义 Skill；
8. 一个无需用户再次提问即可触发的主动场景。

详细排期和完成定义见 [比赛冲刺计划](docs/HomeMind_比赛冲刺计划_2026-08-30_至_2026-09-20.md)。

## 架构

```text
ESP32-S3-EYE / OpenVela
  ├─ 摄像头、麦克风、按键、LCD、LED
  ├─ ai_agent + 白名单工具 + 本地 Skill
  └─ Wi-Fi / TLS / MQTT
             │
             ▼
家庭 Ubuntu 网关 ── MQTT ── FastAPI + SQLite
             │                    │
             │                    ├─ MiMo 文本语义规划
             │                    └─ HTTPS/WSS → 微信小程序
             └─ 真实米家设备桥接
```

原始音频和摄像头画面只在端侧或家庭局域网处理；仅必要文本可以发送给 MiMo。最终材料不会声称所有数据都不经过第三方，因为 MiMo 属于公网模型服务。

## 目录

| 路径 | 用途 |
| --- | --- |
| `firmware/ai_agent_overlay/` | 已验收 ai_agent 源码快照及 SHA-256 |
| `firmware/patches/` | 相对记录基线的 NuttX 最小补丁 |
| `scripts/build.sh` | Linux 环境检查、源码部署、构建和烧录 |
| `tools/deploy-to-vm.sh` | 幂等安装 ai_agent overlay 和 NuttX 补丁 |
| `backend/` | FastAPI、MQTT、WSS、SQLite 和家庭网关 |
| `miniprogram/` | 微信小程序 |
| `app/homemind/skills/` | Skill 设计草案；以设备实际安装验收为准 |
| `artifacts/` | 已验证 BIN/ELF 及 SHA-256 |
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

- 旧 Ubuntu 生产工作区的 `repo`/Git 元数据异常，只作为历史验收现场保留；
- 真实 AI Coding 日志尚未使用参赛者 GitHub 登录名完成官方采集器导出；
- 小程序仍需微信开发者工具干净编译及 HTTPS/WSS 真机验收；
- 摄像头、麦克风、米家设备和主动 Skill 尚未完成最终验收；
- 当前阶段的语义能力会调用 MiMo 公网 API，不能描述为“完全不经过第三方公网”。

## 提交要求

作品以官方团队仓 `dev-ai-contest-2026` 分支为评审事实源。最终提交前必须确认全部 PR 已合并、真实 AI 日志已校验、官方 DOCX/PDF 与不超过 5 分钟的演示视频齐全。参见 [提交合规清单](docs/submission/HomeMind_作品提交合规清单.md)。
