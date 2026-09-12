# 2026-09-12 S2 最小回归：网关 ask 异步与板端网络阻断

日期：2026-09-12。环境：家庭 Ubuntu `192.168.31.251`，串口 `/dev/homemind-esp32`，服务 `homemind-api/gateway/relay/mosquitto` 均为 active。本记录只描述本轮实测与静态根因，不把历史验收当作本轮通过。

## 已恢复/已确认

- 服务健康：`GET /v1/health` → `{"status":"ok","service":"homemind-c1"}`。
- 网关串口句柄陈旧时，`led.on` 会立刻 `Input/output error`；`systemctl restart homemind-gateway` 后串口重新打开。
- 修复后 `device.info` 可在“等待完整回包前”被误判；随后发现更根本的问题：当前固件 `ask` 是异步的。

## 网关缺陷与修复

现象：下发 `led.on` 后约 50ms 内失败 `device did not confirm requested LED state`。

串口原始回包：

```text
ask 打开灯
Sent to agent: 打开灯
vela>
```

固件 `cmd_ask` 只负责入队并打印 `Sent to agent`，真正的工具/模型结果稍后由 outbound 打印：

```text
[Agent]: {"led":"on"}
vela>
```

网关原先 `_read_until_prompt` 见到第一个 `vela>` 就返回，因此永远等不到 `"led":"on"`。

修复：`SerialExecutor.exec` 在看到 `Sent to agent` 后继续读到 `[Agent]:` 与后续 `vela>`，并用 `in_waiting` 非阻塞攒包（`read(n)` 凑不满会阻塞）。

## 板端阻断（本轮未解除）

1. **Wi-Fi 凭据未保存**：`wifi_reconnect` 返回 `No saved credentials. Use: set_wifi <ssid> <pass>`；`net_status` → `Network connected: no` / `IP: 0.0.0.0`。
2. **agent_loop 依赖网络才启动**：`network_watch_task` 在 `network_wait_connected` 成功后才 `agent_loop_start()`。无网时 `ask 打开灯` 只出现 `Sent to agent`，不会出现 `[Agent]`，LED 快速路径也不会执行。
3. **LLM/router 配置为空**：`config_show` 的 API Key/Model/Host 均为 `(not set)`；`router_status` `backend_count=0`。联网后若无 MiMo 路由配置，非快速路径的 `ask` 仍会失败。
4. **多功能房吸顶灯 unavailable**：HA 实体 `light.leishi_cn_940744854_eps127_s_2_light` 状态为 `unavailable`；同账号下客厅大灯等可用。当前白名单仍指向该灯，灯光闭环无法冒充成功。

## 本轮结论

- 家庭 API 入站与网关收令链路正常（queued → acked）。
- 灯光/工具闭环在板端网络恢复前不能记“已验收”。
- 不把串口 `Sent to agent` 或历史 BIN 的 done 当作本轮最终回归结果。

## 解除条件

- 用户提供 Wi-Fi SSID/密码，执行 `set_wifi` 并确认 `net_status` connected。
- 配置可用 MiMo/路由密钥后，重跑：启动、`ask` LED 开/关、`device.info`、Skill 延时执行、以及（若灯恢复）mihome 吸顶灯开关回读。
