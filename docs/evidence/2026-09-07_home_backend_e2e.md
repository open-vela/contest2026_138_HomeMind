# 家庭侧服务底座搭建与端到端验证（2026-09-07，工作包 B 第一步）

> 依据：docs/C1.0-腾讯云资产与后端计划.md（家庭服务迁移）
> 主机：192.168.31.251（家庭 Ubuntu 22.04），操作时间 2026-09-07 13:46–13:53。
> 状态：**已验收**（本机全链路），板端 MQTT 接入与公网入口切换为后续步骤。

## 1. 完成事项

### 1.1 Mosquitto（家庭本地 broker）
- 安装：`mosquitto 2.0.11-1ubuntu1.2` + `mosquitto-clients`（apt，Ubuntu 官方源）
- 服务：`systemctl enable --now mosquitto`，active
- 监听：`127.0.0.1:1883`（Ubuntu 默认仅本地回环，符合"家庭侧不出公网"纪律；板端同网段接入时需在 conf.d 显式放行 LAN + 密码认证——后续步骤）
- 配置：`/etc/mosquitto/mosquitto.conf`（默认，include_dir conf.d）+ 持久化 `/var/lib/mosquitto/`
- 验证：`mosquitto_pub`/`mosquitto_sub` 往返成功（`homemind-mqtt-ok-…`）

### 1.2 家庭 FastAPI + SQLite（homemind-api）
- 代码：复用云端 C1 实现 `backend/api/app/`（官方仓），复制到独立部署目录 `/home/hfy/homemind-backend/app/`（官方仓不动）
- venv：`/home/hfy/homemind-backend/venv`（Python 3.10.12；fastapi 0.111.0、SQLAlchemy 2.0.30、paho-mqtt 1.6.1）
- 配置：`/home/hfy/homemind-backend/.env`（0600；JWT_SECRET 随机 48B；SQLITE_PATH=/home/hfy/homemind-data/homemind.db；MQTT_BROKER_HOST=127.0.0.1:1883；ALLOWED_ACTIONS=led.on,led.off,device.info；家庭侧不直接微信登录，WX_APPID 留空）
- 服务：systemd `homemind-api.service`（EnvironmentFile=.env，ExecStart=uvicorn app.main:app --host 127.0.0.1 --port 8001，Restart=always），active
- 监听：`127.0.0.1:8001`（仅本机，公网入口由后续反向通道处理）
- SQLite：`/home/hfy/homemind-data/homemind.db`，表 users / device_bindings / commands / device_status（复用 C1 模型）
- 启动即连本地 MQTT：日志 `mqtt connected to 127.0.0.1:1883`、`rc=0`

### 1.3 端到端验证（已验收）
链路：JWT 鉴权 → 绑定设备 → 下发命令 → 本地 MQTT 收到 → SQLite 落库

| 步骤 | 请求 | 结果 |
| --- | --- | --- |
| 建用户+JWT | security.create_tokens | token 208 字符 |
| POST /v1/devices | bind esp32s3-eye | `{"device_id":"esp32s3-eye","name":"HomeMind-EYE"}` |
| POST /v1/devices/esp32s3-eye/commands | led.on ttl=30 | `{"command_id":"3b6d064d…","status":"queued"}` |
| GET /v1/commands/{id} | 查状态 | queued, ttl 30, created_at 2026-09-07T13:52:00 |
| mosquitto_sub device/esp32s3-eye/cmd | 旁路监听 | `{"command_id":"3b6d064d…","action":"led.on","params":{"ttl":30},"ttl":30,"ts":1788789120}` |
| SQLite 查询 | users/bindings/commands | 各 1 行，commands.status=queued |

## 2. 部署清单（家庭主机现状，2026-09-07 13:53）

| 服务 | 端口 | 状态 | 数据/配置 |
| --- | --- | --- | --- |
| mosquitto | 127.0.0.1:1883 | active（自启） | /etc/mosquitto/ + /var/lib/mosquitto/ |
| homemind-api | 127.0.0.1:8001 | active（自启） | /home/hfy/homemind-backend/（代码+venv+.env）+ /home/hfy/homemind-data/homemind.db |
| homemind-gateway | （串口） | active（原有） | /home/hfy/homemind-gateway/（未改动） |
| homeassistant | 8123 | active（原有，docker） | /home/hfy/homeassistant/config/（未改动） |

## 3. 备份（迁移前置，2026-09-07 13:47 完成）

- 目录：`/home/hfy/backups/2026-09-07-migration-pre/`（25 MB）
- 内容：gateway.env.bak（0600）、gateway_agent.py.bak、mihome_adapter.py.bak、command_policy.py.bak、HA-config-snapshot/（sudo 完整复制 config 卷）、home-assistant_v2.db.bak（Python sqlite3 online backup，5.0 MB，一致性备份）

## 4. 边界与后续（未越界）

- 官方仓 `backend/` 未改动（部署为复制副本）；网关 / HA / 公网入口均未改动。
- 后续（09-08 起）：Mosquitto 放行 LAN + 密码认证；板端接入 `device/{id}/cmd` 与 ack/status 回传（连通命令状态机 queued→acked→done）；网关新增本地 MQTT 目标；家庭业务接口 /v1/intents、/v1/events、/v1/tasks、/v1/assets 落地；迁移核对后处理公网业务写入停用。
- 已知差异待办：gateway_agent.py 现场版与官方仓版（播报重试）不一致，迁移网关时以官方仓版为基线。
