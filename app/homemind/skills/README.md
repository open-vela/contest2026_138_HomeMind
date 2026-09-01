# HomeMind Runtime Skills

ai_agent 当前的 Skill loader 扫描技能目录下的平铺 `.md` 文件，不扫描 `SKILL.md` 子目录。当前这份 ESP32-S3-EYE 固件的 `AGENT_SKILLS_DIR` 实际解析为 `/data/ai_agent/skills/`；比赛文档中的 `/data/agent/skills/` 是目标验收路径，若最终基线不同，必须以烧录固件的实际宏定义和真机路径为准。每个文件的第一行必须是 `# 标题`，标题后的第一段作为技能摘要。

## 当前 Skill

- `home_security.md`：一次性、15 秒延迟的板载 LED 主动执行演示。

## 安装与验收

1. 将 `home_security.md` 以原文安装为当前固件的 `/data/ai_agent/skills/home_security.md`（或最终基线实际配置的技能目录）。
2. 在设备执行 `/skill` 或 `ask 技能列表`，确认摘要出现“HomeMind 主动安防演示”。
3. 发送“开始主动安防演示”，确认先创建 cron 任务，而不是立即声称 LED 已改变。
4. 等待约 15 秒，保留 `Cron job firing`、`Executing action: led_control`、`Action led_control OK` 和实际 LED 状态。
5. 确认单次任务执行后删除，不重复执行。

静态验收只能证明文件格式、工具白名单和危险指令约束；不能替代设备加载、触发、执行和视频证据。
