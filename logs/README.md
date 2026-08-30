# AI Coding 日志

本目录保存大赛 AI Coding 对话日志。日志采集、目录格式和提交方式以官方手册为准。

## 1. 安装采集器

完成完整 OpenVela 工作区的 `repo init` 和 `repo sync` 后，在 Ubuntu 中进入本比赛仓执行：

```bash
cd ~/work/openvela/contest2026_138_HomeMind

bash ../.claude/skills/contest-log-collector/onboarding/install.sh \
  --team-id contest2026_138_HomeMind \
  --github-login <your-github-login>

bash ../.claude/skills/contest-log-collector/onboarding/verify-setup.sh
```

每位队员都必须使用自己的 GitHub 登录名安装和检查。

## 2. 自动归集规则

- 采集器只在能够向上找到 `.repo/` 的完整 OpenVela 工作区内启用。
- 在工作区内结束受支持 AI 工具的会话后，日志自动写入本仓 `logs/`。
- 采集器不会自动执行 `git push`；上传仍由参赛者控制。
- Windows 本地独立仓不包含 `.repo/` 时，对话可能不会被大赛采集器自动归集。需要计入比赛的开发会话应在完整 OpenVela 工作区内进行。

官方目前支持 Claude Code/AIoT-IDE、OpenCode 和 Codex。其他工具是否计入有效日志以官方手册最新说明为准。

## 3. 目录格式

```text
logs/
└── <github-login>/
    ├── manifest.json
    └── <YYYY-MM-DD>/
        └── <tool>__<session-id>.jsonl
```

不要自行改写 `.jsonl` 内容或把多次会话拼接成自定义 JSON 文件。

## 4. 检查与提交

```bash
# 查看已采集会话
contest-snapshot --list

# 确认日志已自动入仓
ls -lt logs/<your-github-login>/

# 合规性检查
python3 ../.claude/skills/contest-log-collector/tools/validate-log.py logs/

# 提交
git add logs/
git commit -s -m "logs: sync AI sessions"
git push
```

若需要手动重新同步当天会话：

```bash
contest-snapshot --today
contest-snapshot --today --confirm
```

不加 `--confirm` 时只进行预览。

## 5. 隐私与安全

- 提交前检查会话是否包含 API Key、Wi-Fi 密码、个人地址或其他隐私信息。
- 如需撤回整个会话，在提交前删除对应 `.jsonl` 文件；不要编辑日志正文。
- 工作区外的私人对话不会被采集器归集。
- 定期提交日志，不要等到截止日前集中处理。

## 6. 官方参考

- [AI Coding 日志归集与提交手册](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/ai_coding_log_guide.md)
