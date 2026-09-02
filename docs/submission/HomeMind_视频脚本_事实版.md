# HomeMind 演示视频事实版脚本

> 版本：2026-09-02。建议成片 4 分钟以内；所有“展示”动作都必须重新用真实板、真实服务和真实输出录制。没有证据的能力不放入镜头。

## 0:00–0:25 定位与实物

- 镜头同时出现 ESP32-S3-EYE、LCD、Ubuntu 网关终端。
- 口播：HomeMind 是运行在 OpenVela/Apache NuttX 与 ai_agent 上的家庭终端；本版本已验收自动联网、MiMo 文本问答、白名单工具、运行时 Skill 和 MQTT 设备侧闭环。
- 屏幕只展示架构，不出现 Wi-Fi 密码、API key、AppSecret、JWT 或 MQTT 密码。

## 0:25–1:10 OpenVela / ai_agent 真机启动

- 录制复位后进入 `ai_agent`，展示 `net_status` 的真实联网结果和 LCD 状态。
- 执行一条短的真实 `ask`，保留 MiMo TLS/HTTP 200 和非固定回复的串口证据；若本轮失败，保留失败并说明，不剪成成功。

## 1:10–2:00 Skill 与主动执行

- 展示仓库 `app/homemind/skills/home_security.md` 和静态校验结果。
- 在设备执行 `ask list skills`，展示 `Home Security Skill` 被发现。
- 执行一次主动场景启动请求，录到 LCD/LED 的真实反馈；等待约 15 秒后录到 LED 事件，再执行停止请求，展示任务清理结果。
- 口播明确：这是一次受限的板载 LED 主动演示，不把它说成摄像头告警。

## 2:00–2:55 MQTT 设备侧闭环

- 终端运行脱敏的 MQTT 测试脚本，分别发送 `led.on` 与 `led.off`。
- 画面同时保留板载 LED 变化、终端的 `acked → done` 和对应 `status: on/off`。
- 口播明确链路：TLS MQTT broker → 家庭 Ubuntu 网关 → `/dev/ttyACM0` → ai_agent → LED → ACK/status。
- 展示命令 ID 可被追踪；不展示 broker 用户名密码。

## 2:55–3:30 稳定性与隐私边界

- 展示 `artifacts/SHA256SUMS`、干净构建/烧录记录和长稳回归结果的文件路径。
- 口播：原始音视频不上传 MiMo；发送给 MiMo 的是用户确认的必要文本。MiMo 是公网模型服务，因此不宣称“完全不经过第三方”。

## 3:30–4:00 已知限制

- 必须如实说：当前板端没有 `/dev/video0`、音频/I2S 节点，尚未实现“你好，openvela”离线唤醒和摄像头实时感知；也没有真实米家设备桥接。
- 微信小程序的代码已完成真实 API 对接准备，但开发者工具清洁编译、微信真机登录/WSS、云端数据库闭环尚未取得验收证据。
- 片尾展示 `STATUS.md` 与 `docs/evidence/`，让视频、仓库和报告保持同一事实口径。

## 录制前门禁

- 实物照片、视频和终端窗口逐帧检查，不得出现任何密钥、密码、私有音频或私人信息。
- 总时长不超过 5 分钟；保留一次连续真机过程，剪辑只压缩等待，不隐藏失败。
- 若最终补齐摄像头、唤醒、米家或小程序真机证据，必须先更新本脚本和 `STATUS.md`，再重新录制对应段落。
