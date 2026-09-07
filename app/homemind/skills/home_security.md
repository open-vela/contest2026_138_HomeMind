# HomeMind 主动安防演示
用于 HomeMind ESP32-S3-EYE 的可审计主动执行演示：用户只需设置一次任务，设备在稍后自动执行板载 LED 动作并回报结果。

## 适用场景

当用户说“开始主动安防演示”“设置离家安防演示”或“稍后自动检查设备”时使用。此 Skill 的主动性来自 `cron_add` 的延迟任务：设置完成后不需要用户再次输入命令。

## 能力边界

- 只允许使用当前固件已注册的 `get_current_time`、`cron_add`、`led_control` 和 `get_device_info`。
- 不调用 shell，不生成任意 GPIO、MQTT topic、设备地址或文件路径。
- 不把摄像头、人脸识别、米家设备、短信或小程序能力描述为已实现；这些能力未由本 Skill 冒充。
- 所有工具失败都必须原样说明；不能把“已排队”说成“已执行”。

## 主动执行流程

1. 调用 `get_current_time` 获取设备当前 epoch，不凭记忆生成时间戳。
2. 计算 `at_epoch = 当前 epoch + 15`，只允许延迟 15 秒的单次演示。
3. 调用 `cron_add`，参数必须符合下面的结构：

```json
{
  "name": "HomeMind 主动安防演示",
  "schedule_type": "at",
  "at_epoch": 0,
  "action": "led_control",
  "action_args": "{\"action\":\"toggle\"}",
  "message": "HomeMind 主动安防演示已执行，请查看板载 LED 状态。",
  "delete_after_run": true
}
```

4. 将 `at_epoch` 替换为真实计算值；保持 `action`、`action_args` 和 `delete_after_run` 不变。
5. 任务设置成功后，回复“已设置，约 15 秒后自动执行”，不要声称 LED 已经变化。
6. 自动任务触发后，系统会直接执行 `led_control`，并向原会话发送执行通知。若需要核对结果，再调用 `led_control` 的 `status` 或 `get_device_info`。

## 安全失败规则

- `get_current_time` 失败：停止，不创建任务。
- `cron_add` 返回错误：报告创建失败，不说“稍后会执行”。
- 任何请求要求立即控制、周期任务、米家设备、摄像头、识别人脸或发送公网通知时，说明本 Skill 当前只支持一次、15 秒后的板载 LED 主动演示，并等待用户明确改用已有能力。
- 禁止使用 `run_shell`、`write_file`、`edit_file`、`mcp_*` 或未列入能力边界的工具。

## 验收日志关键词

真机验收时应在串口日志中同时保留：

- Skill 文件出现在 `技能列表` 或 `/skill` 摘要中；
- `get_current_time` 成功；
- `cron_add` 返回任务 ID；
- 到期后出现 `Cron job firing` 和 `Executing action: led_control`；
- 出现 `Action led_control OK` 及 LED 状态结果；
- 单次任务执行后被删除，不重复执行。

## 当前事实

本文件是比赛仓库中的运行时 Skill 源文件，采用 ai_agent 当前实现支持的平铺 `.md` 格式。本次已验证的固件配置将它加载为 `/data/ai_agent/skills/home_security.md`；若最终比赛基线将 `AGENT_SKILLS_DIR` 配置为 `/data/agent/skills/`，必须在烧录后以该路径重新验收。无论路径如何，静态文件存在不等于设备已加载，提交前仍必须补充真机日志和视频。
