# HomeMind 家庭网关 gateway-agent

部署在家庭 Ubuntu（192.168.31.251），**只主动出站**，不开放任何公网入站端口。

## 职责
- 连接 `mqtts://hfy-ai.cloud:8883`（TLS + 用户名/密码；当前证书覆盖根域）
- 订阅 `device/esp32s3-eye/cmd`
- 通过 `/dev/ttyACM0` 串口进入 `ai_agent` 会话，执行 `ask 打开灯/关灯`
- 发布 `device/esp32s3-eye/ack`（acked→done / expired）与 `device/esp32s3-eye/status`
- 可选：通过 `mihome_adapter.py` 调用用户自有 Home Assistant 中的官方 Xiaomi Home Integration；只允许显式白名单的 `light.*`/`switch.*`，执行后回读状态
- 指数退避重连
- 校验 `command_id/action/ts/ttl`，拒绝过期、未来时间戳和非白名单动作；进程内抑制 MQTT QoS1 重复投递
- 只有串口返回成功且 LED 状态与目标一致时才发布 `done`，否则发布 `expired`

## 安装
```bash
cd gateway-agent
python3 -m venv .venv && . .venv/bin/activate
pip install -r requirements.txt
cp .env.example .env   # 填入 deploy.sh 下发的 MQTT_PASS
```

## 运行
```bash
python3 gateway_agent.py
# 或用 systemd / nohup 常驻
```

## 备注
- `CMD_MODE=ask` 为当前打通闭环方案（经设备 LLM 自然语言执行）。
- 米家登录阻塞已由用户于 2026-09-06 确认解除。米家动作仍使用 `mihome.set_power` / `mihome.get_state`，只在 `HOMEMIND_HA_URL`、`HOMEMIND_HA_TOKEN` 和 `HOMEMIND_MIHOME_ALLOWED_ENTITIES` 全部配置后启用；没有这些配置时 fail-closed。真实设备导入、开关、状态回读和小程序联动仍需取得验收证据。
- 后续可在设备端新增「云端直接命令通道」后切 `CMD_MODE=direct`，绕开 LLM 路径、降低延迟。
- 局域网自签证书联调时，把自签 `fullchain.pem` 传到本机并设 `TLS_CA_PATH` 指向它；
  正式 Let's Encrypt 证书下发后留空 `TLS_CA_PATH` 即可用系统 CA 校验。

## 本地门禁测试

```bash
python3 -m pytest backend/gateway-agent/tests -q
```

测试覆盖有效命令、过期/未来时间戳、非法动作、畸形命令和重复投递。

## 2026-09-02 实机段落验证

家庭 Ubuntu 网关已安装并运行本目录的网关代码。通过真实 TLS MQTT broker 各发送一次
`led.on` 和 `led.off`，均观察到 `acked -> done`，并收到对应 `status` 的 `on/off`；
完整边界记录见 `../../docs/evidence/2026-09-02_mqtt_gateway_loop.txt`。这不等同于
FastAPI 数据库、WSS 或微信真机验收，后者需要公共 API 可用和真实小程序操作。

2026-09-03 公网只读门禁已恢复：`https://hfy-ai.cloud/v1/health` 返回 200，OpenAPI
可读，未授权设备请求返回 401，WSS 无效 token 返回 403。真实微信登录码、WSS 消息
交换和小程序真机仍需在微信开发者工具中完成。

后续路线见 `../../docs/HomeMind_后续路线与评估_2026-09-06.md`。
