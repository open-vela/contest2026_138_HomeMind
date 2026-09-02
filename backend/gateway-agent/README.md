# HomeMind 家庭网关 gateway-agent

部署在家庭 Ubuntu（192.168.31.251），**只主动出站**，不开放任何公网入站端口。

## 职责
- 连接 `mqtts://api.hfy-ai.cloud:8883`（TLS + 用户名/密码）
- 订阅 `device/esp32s3-eye/cmd`
- 通过 `/dev/ttyACM0` 串口进入 `ai_agent` 会话，执行 `ask 打开灯/关灯`
- 发布 `device/esp32s3-eye/ack`（acked→done / expired）与 `device/esp32s3-eye/status`
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
