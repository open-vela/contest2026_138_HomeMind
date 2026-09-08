# 家庭出站通道与入口转发（relay）端到端验收（2026-09-07）

> 范围：C1.0「09-08~09-10：家庭出站通道和入口转发」的通道主体落地。目标架构：
> 微信小程序 ↔ 腾讯云入口（微信登录/转发）↔ 家庭主动出站连接 ↔ 家庭 FastAPI/SQLite/Mosquitto。
> 状态口径：已验收（家庭侧通道）/ 部分完成（云端侧部署依赖云主机凭据）/ 未实现（小程序真机）。

## 1. 架构与协议

```
小程序 → HTTPS/WSS 腾讯云入口 → 云端 API（RELAY_MODE=true）
        → 云端 broker 发布 homemind/relay/req（QoS1）
        → 家庭 relay（只主动出站，mqtts 8883 TLS）订阅 req
        → 调用家庭 API 127.0.0.1:8001（Authorization 透传）
        → 响应发布 homemind/relay/resp → 云端 API 同步等待返回
```

- 请求 payload：`{req_id, method, path, body, authorization}`；响应：`{req_id, status, body, error}`。
- **身份**：云端与家庭共用 `JWT_SECRET`；云端微信登录签发 JWT，relay 透传 Authorization，家庭 API 直接校验（`get_current_user` 不变）。
- **业务真相在家庭**：intents/events/tasks/assets 请求经通道落家庭 SQLite，云端不落业务正文。
- **边界**：auth/health 云端本地处理；命令下发（/v1/devices/.../commands）本期仍走云端现链路（网关双 MQTT），转发切换列为二期，避免双链路状态机冲突。

## 2. 新增代码（官方仓 backend/）

| 文件 | 作用 |
| --- | --- |
| `home-relay/relay_agent.py` | 家庭出站通道服务（paho，只出站，断线指数退避，req_id 校验） |
| `home-relay/.env.example` / `requirements.txt` | 配置模板与依赖 |
| `api/app/relay_client.py` | 云端转发器：发布 req + 同步等待 resp（threading.Event） |
| `api/app/routers/relay_proxy.py` | 转发路由（intents/events/tasks/assets 的 GET/POST/PATCH） |
| `api/app/config.py` | 新增 RELAY_MODE / RELAY_TIMEOUT |
| `api/app/mqtt_client.py` | 订阅 homemind/relay/resp（RELAY_MODE 时）并回调转发器 |
| `api/app/main.py` | RELAY_MODE 条件挂载：云端挂转发路由，家庭副本挂本地路由 |
| `backend/.env.example` | 新增转发模式配置说明 |

## 3. 家庭侧部署与验收（已验收）

部署：`/home/hfy/homemind-relay/`（venv + .env 0600 + systemd `homemind-relay`，active）。
本地 mosquitto：新增用户 `home-relay`（read `homemind/relay/req` / write `homemind/relay/resp`）；
模拟云端入口用户 `mock-cloud`（write req / read resp，权限=生产云端 API 所需）。

回环验收（2026-09-07 15:13，本地模拟云端入口 → relay → 家庭 API）：

| 用例 | 结果 |
| --- | --- |
| POST /v1/tasks（带 JWT） | resp status=200；任务落家庭 SQLite（tasks 2 条，`relay-e2e 测试待办-2` pending）✓ |
| GET /v1/tasks?limit=5（带 JWT） | resp status=200，返回 2 条 ✓ |
| POST /v1/tasks（无 JWT） | resp status=401，body `{"detail":"missing token"}` ✓ |
| relay 服务 | systemd active，转发日志完整（转发请求→已回传 status=200）✓ |

## 4. 生产部署手册（云端侧，待云主机凭据执行）

1. **云端 API**：`.env` 设 `RELAY_MODE=true`、`RELAY_TIMEOUT=20`；**JWT_SECRET 与家庭 .env 一致**（SFTP/安全通道分发，绝不入仓）。
2. **云端 broker ACL**（mosquitto 容器）：API 用户 `write homemind/relay/req`、`read homemind/relay/resp`；新增 relay 用户（如 `home-relay`）`read homemind/relay/req`、`write homemind/relay/resp`。
3. **家庭 relay 生产配置**（`/home/hfy/homemind-relay/.env`）：`RELAY_MQTT_HOST=api.hfy-ai.cloud`、`RELAY_MQTT_PORT=8883`、`RELAY_TLS=true`、`RELAY_MQTT_USER/PASS`=云端新增 relay 用户。
4. 重建云端 api 镜像并 `docker compose up -d`；验证 `/v1/health` 与转发路由（小程序真机为最终验收）。
5. 入口转发验收后：停用云端业务写入（处理旧 DB/备份/日志业务正文，记录最终位置）。

## 5. 外部依赖 / 保留项（如实）

- 云端 API 改造代码已入库并完成逻辑设计，**部署到云主机需 SSH 凭据**（106.53.25.19:22 可达，凭据未提供）。
- 微信域名登记（request https://hfy-ai.cloud / socket wss://hfy-ai.cloud）与小程式真机验收未完成。
- 命令下发通道切换 relay 转发列为二期。
- 官方仓未 push（既定纪律）。
