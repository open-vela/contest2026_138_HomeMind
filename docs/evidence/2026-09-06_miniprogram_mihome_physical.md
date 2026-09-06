# 小程序 mihome 命令 → 实际灯光 物理闭环确认 — 2026-09-06

性质：实机验收证据（微信开发者工具模拟器 + 真实灯光物理确认）。

## 链路

微信开发者工具模拟器（AppID `wx75312eb43495775f`，miniprogram 设备页"米家设备控制"卡片）
→ `POST /v1/devices/esp32s3-eye/commands`（HTTPS，`hfy-ai.cloud`）
→ 腾讯云 FastAPI（`ALLOWED_ACTIONS` 已含 mihome 动作，workbuddy 2026-09-06 完成）
→ MQTT TLS → 家庭网关 `gateway_agent.py` → `mihome_adapter.py`
→ Home Assistant REST → Xiaomi Home 集成 → 领普 `linp.switch.t2dbw1`（阳台开关）
→ 继电器 → 阳台实际灯光

## 实测记录

1. 小程序下发 `mihome.set_power on`（`params:{entity_id:"switch.linp_cn_950233435_t2dbw1_on_p_2_1"}`）→ 命令流 `queued → acked → done`，HA 实体状态 `on`（用户模拟器操作）。
2. 服务器侧经同一网关切回 `off` → **用户现场确认实际灯光熄灭**。
3. 服务器侧再切回 `on` → **用户现场确认实际灯光点亮**。
4. 开/关两个方向的"物理动作确认"均由用户当场目视完成；HA 状态回读与物理状态一致，无虚报 `done`。

## 本轮小程序改动（本地完成，随本记录提交）

- `pages/devices/`：新增米家控制卡片（实体 ID 输入持久化 + 客户端白名单正则校验 + 开/关/查状态按钮 + 结果回显）。
- `utils/api.js`：修复模拟器不走登录的根因——缓存 token 使 `login()` 短路；新增 401 清除 token + `authRequest` 强制重登重试一次的自愈路径。
- `login()` 与命令/查询全部走 `authRequest`；esprima JS 语法与 JSON 校验通过。

## 边界（不得写成已完成）

- 本轮验收在**微信开发者工具模拟器**完成，手机真机预览（强制正式域名校验）尚未执行。
- 微信公众平台服务器域名（request `https://hfy-ai.cloud`、socket `wss://hfy-ai.cloud`）登记状态待用户确认。
- `mihome.get_state` 的具体状态值未透传到小程序 UI（云端 status 事件不含 `mihome_result` 明细），仅为后续增强项，不阻塞闭环。
- LED 开关经同一小程序链路此前已验证；云端 `.env` 变更由 workbuddy 执行，本轮实测间接确认生效。
