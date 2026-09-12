# 2026-09-12 S2 最小回归：板载 LED 闭环通过；吸顶灯仍不可用

日期：2026-09-12。设备固件为现场 `artifacts/nuttx.bin`（SHA `9cfe649e…` 一系）。家庭侧 Ubuntu `192.168.31.251` 服务 active；板端已 `set_wifi` 获取 `192.168.31.252`。

## 阻断解除

- 板端原先无保存 Wi-Fi，`agent_loop` 不启动；写入家庭 SSID 后 `net_status` connected。
- 网关 `ask` 异步回包已修复：等到 `[Agent]: {"led":"on"|"off"}` 才标记 done（见 `2026-09-12_s2_gateway_async_ask_blockers.md`）。

## 本轮实测

| 项目 | 结果 | 说明 |
| --- | --- | --- |
| API→MQTT→网关→串口→板载 LED 开 | 10/10 done | 每次约 1s，回包含 `"led":"on"` |
| API→MQTT→网关→串口→板载 LED 关 | 10/10 done | 每次约 1s，回包含 `"led":"off"` |
| `device.info`（MiMo/工具链路） | 1/1 done | 约 12s，走非快速路径 |
| 多功能房吸顶灯 | **未验收** | HA 实体 `light.leishi_cn_940744854_eps127_s_2_light` 仍为 `unavailable` |
| `mihome.set_power` 闭环 | **未执行** | 目标灯不可用；部署侧 `ALLOWED_ACTIONS` 已补回 mihome 动作，待灯恢复后复测 |

## 代码修复入库

- `backend/gateway-agent/gateway_agent.py`：异步 `[Agent]` 等待。
- `backend/api/app/routers/relay_proxy.py`：补 `await`，用 `JSONResponse` 传递下游状态码。

## 边界

- 本记录只覆盖板载 LED 与 `device.info` 的本轮实机结果，不外推为米家灯/小程序/长稳已通过。
- 吸顶灯恢复前不得把模拟动作写成实物闭环。
