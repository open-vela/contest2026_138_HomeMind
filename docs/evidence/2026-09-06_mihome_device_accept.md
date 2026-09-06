# 真实米家设备 mihome.set_power / mihome.get_state 验收 — 2026-09-06

性质：实机验收证据。设备：公望府 阳台 区域的"阳台开关"（领普 `linp_cn_950233435_t2dbw1` 墙壁开关），实体 `switch.linp_cn_950233435_t2dbw1_on_p_2_1`。链路：MQTT（TLS，`api.hfy-ai.cloud:8883`）→ 家庭网关 `gateway_agent.py` → `mihome_adapter.py` → Home Assistant REST（`127.0.0.1:8123`）→ Xiaomi Home 集成 → 真实设备。HA 长期访问令牌由用户提供，只存于服务器 `.env`（0600），未进入仓库或日志。

## 前置修复

- 首次直连验收发现：`turn_off` 服务调用成功但 HA 状态回读滞后，适配器单次回读误判失败。已在 `backend/gateway-agent/mihome_adapter.py` 的 `set_power` 增加 3 秒确认窗口（0.4 秒间隔轮询，超时仍未确认则失败，保持 fail-closed）；新增单元测试 `test_set_power_polls_until_state_confirms`，4 项测试全部通过后重新部署并重启网关（服务 active，串口/MQTT 正常）。
- 命令门禁按 `command_policy.py` 执行：白名单动作、command_id、ttl≤300、时间戳校验、QoS1 重放抑制。

## 实测记录（2026-09-06，服务器本地时间 UTC）

1. **直连适配器验收**（修复后）：
   - `get_state` 初始 `off`；
   - `set_power on` → `{"ok": true, "state": "on"}`，回读确认；
   - `set_power off` → `{"ok": true, "state": "off"}`，回读确认；
   - 最终状态 `off`。
2. **MQTT 全链路验收**（发布到 `device/esp32s3-eye/cmd`，订阅 `ack`/`status`）：
   - `mihome-accept-on-0906a` `mihome.set_power on=True` → `acked → done`，`mihome_result={"ok": true, "state": "on"}`；
   - `mihome-accept-off-0906a` `mihome.set_power on=False` → `acked → done`，`mihome_result={"ok": true, "state": "off"}`；
   - `mihome-accept-get-0906a` `mihome.get_state` → `acked → done`，`mihome_result` 返回真实状态 `off` 与属性（`friendly_name: 阳台开关 开关 开关`）。
3. **重复开/关回归**：4 个命令 `mihome-accept-rep0..3-0906`（on/off/on/off 交替）→ 4/4 `acked → done`。
4. **收尾设备状态**：`off`（与验收开始前一致）。

## 结论与边界

- 一台真实米家设备的 `mihome.set_power`（开/关各多次）与 `mihome.get_state` 状态回读已通过 MQTT→网关→HA→设备全链路验收，失败路径的确认逻辑（回读不匹配不发 `done`）在修复后保留。
- 白名单当前仅 1 个实体（`switch.linp_cn_950233435_t2dbw1_on_p_2_1`）；其他设备默认拒绝。
- 本记录只覆盖米家设备开关闭环。小程序真机登录/绑定/命令回显、连续音频/离线唤醒、最终视频与提交材料仍为未完成项。
