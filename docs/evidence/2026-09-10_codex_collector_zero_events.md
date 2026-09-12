# 2026-09-10 官方日志采集器对 Codex rollout 采集为 0 事件（实测）

## 结论

官方采集器 `contest-log-collector@1.3.0` 的 **Codex 通道对当前 Codex CLI 的 rollout 格式无效**：
处理一份真实 rollout 后 staging 目录为空、无 `captured N event(s)` 输出，即 **0 events**。
因此「在 Ubuntu openvela 工作区内用 Codex 开发 = 自动入仓」这条路径当前不成立，
2026-09 的日志只能按本文记录的口径手工补录。

## 复现步骤（Ubuntu 22.04 / 192.168.31.251）

```bash
cd ~/work/openvela-clean-20260830/contest2026_138_HomeMind   # 有 .repo/ 的工作区
export TEAM_ID=contest2026_138_HomeMind
export GITHUB_LOGIN=Miles-hfy
export SESSION_LOG_DIR=/tmp/hm_stage_test

echo '{"session_id":"test-codex-0001","cwd":"'$PWD'",\
"transcript_path":"/tmp/hm_test_rollout.jsonl","hook_event_name":"Stop"}' \
  | python3 ~/.claude/contest-shared/snapshot_core.py --tool codex
```

实测输出（2026-09-10）：

```
[session-log] tool=codex team=contest2026_138_HomeMind version=1.3.0
```

- 没有 `captured N event(s)` 行 → `process_claude_stdin()` 走到
  `if not contest_events: ... return 0`；
- `find /tmp/hm_stage_test -type f` 结果为空 → 未写入任何 `.jsonl`；
- 源文件 `rollout-2026-09-06T13-08-59-01a0751e-aa5c-7eb3-aa73-78a012d616e7.jsonl`
  （123,367 B，人工确认含 3 条真实 user/assistant 消息）。

测试文件已删除（`rm -rf /tmp/hm_stage_test /tmp/hm_test_rollout.jsonl`）。

## 根因（源码级）

`adapters/codex/hooks/snapshot.py` 只是薄封装，`os.execvp` 转调共享的
`adapters/shared/snapshot_core.py --tool codex`，与 Claude Code 共用
`expand_claude_event()`。该函数按 Claude Code transcript 结构取值：

```python
top_type = raw_event.get("type")      # 期望 user / assistant / system
msg      = raw_event.get("message")   # 期望 .message 里是真正的对话
content  = msg.get("content")
```

而当前 Codex rollout 每行的结构是：

```json
{"timestamp":"...","type":"response_item",
 "payload":{"type":"message","role":"user","content":[{"type":"input_text","text":"..."}]}}
```

差异三处：

1. 顶层 `type` 是 `response_item`（不在 `expand_claude_event` 的 skip 列表里，被当作普通事件处理）；
2. 对话体在 `payload` 里，`raw_event.get("message")` 恒为 `None` → `msg = {}`；
3. `content` 为 `None`，既非 `str` 也非 `list` → 函数直接 `return []`。

`snapshot_core.py` 顶部注释写着「Codex source comments reference Claude Code's schema
directly」，即实现假设两者 schema 一致；该假设在当前 Codex 版本上不成立。

## 影响与应对

| 影响 | 应对 |
| --- | --- |
| Ubuntu 工作区内用 Codex 不会自动入仓 | 继续用 `transfer/export_sept_sessions.py` 手工补录，输出与官方 schema v1.0 一致 |
| `contest-snapshot --backfill` 只扫 Claude Code transcript，对 Codex 无效 | 已确认导入 0 |
| 后续开发想自动采集 | 改用 Claude Code（`expand_claude_event` 的注释明确按 `~/.claude/projects/*.jsonl` 验证过），或等组委会修采集器 |
| 是否报官方 | 属于手册 FAQ Q6「工具 bug」，可在技术支持群报；**不要**自己改 `.claude/` 工具仓 |

## 手工补录口径（与采集器保持一致）

- 事件字段：`schema_version / session_id / team_id / github_login / tool / seq` + `ts / role / text|thinking|tool_name|tool_call_id|input|output`；
- 脱敏沿用官方 `DEFAULT_REDACT_RULES`（`sk-*` → `sk-***REDACTED***`、`ghp_*`、`Bearer xxx`），
  另加 Ubuntu SSH 密码规则；
- 逐事件记 `redacted_count`，逐会话记 `redacted_count_total`；
- 产出后必须跑 `python3 ../.claude/skills/contest-log-collector/tools/validate-log.py logs/`
  确认 `ALL OK`。

本次补录结果：14 files / 2016 events / **ALL OK**，commit `af0794f`。
