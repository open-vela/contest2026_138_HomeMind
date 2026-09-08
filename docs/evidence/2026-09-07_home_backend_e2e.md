# 家庭侧服务底座搭建与端到端验收（2026-09-07，工作包 B 第一步/第二步）

> 依据：docs/C1.0-腾讯云资产与后端计划.md（家庭服务迁移）
> 主机：192.168.31.251（家庭 Ubuntu 22.04），操作时间 2026-09-07 13:46–14:00。
> 状态：**已验收**（含板端真实执行全链路），公网入口切换为后续步骤。

## 1. 完成事项

### 1.1 Mosquitto（家庭本地 broker，已认证放行 LAN）
- 安装：`mosquitto 2.0.11-1ubuntu1.2` + `mosquitto-clients`（apt，Ubuntu 官方源）
- 服务：`systemctl enable --now mosquitto`，active；配置 `/etc/mosquitto/conf.d/10-homemind.conf`
- 监听：`0.0.0.0:1883`（LAN 可接入），`allow_anonymous false` + `password_file /etc/mosquitto/passwd`（root:mosquitto 640，修复了启动 status=13 权限问题）
- 用户：`home-gateway`（网关用）、`home-api`（API 用），凭据仅存 .env（0600）
- 验证：匿名连接被拒（Connection Refused: not authorised）；认证 pub/sub 往返通过

### 1.2 家庭 FastAPI + SQLite（homemind-api，MQTT 认证连接）
- 代码：复用云端 C1 实现 `backend/api/app/`（官方仓），复制到独立部署目录 `/home/hfy/homemind-backend/app/`（官方仓不动）
- venv：`/home/hfy/homemind-backend/venv`（Python 3.10.12；fastapi 0.111.0、SQLAlchemy 2.0.30、paho-mqtt 1.6.1）
- 配置：`/home/hfy/homemind-backend/.env`（0600；JWT_SECRET 随机 48B；SQLITE_PATH=/home/hfy/homemind-data/homemind.db；MQTT_BROKER_HOST=127.0.0.1:1883；**MQTT_USERNAME=home-api + MQTT_PASSWORD（broker 认证）**；ALLOWED_ACTIONS=led.on,led.off,device.info；家庭侧不直接微信登录，WX_APPID 留空）
- 服务：systemd `homemind-api.service`（EnvironmentFile=.env，ExecStart=uvicorn app.main:app --host 127.0.0.1 --port 8001，Restart=always），active
- 监听：`127.0.0.1:8001`（仅本机，公网入口由后续反向通道处理）
- SQLite：`/home/hfy/homemind-data/homemind.db`，表 users / device_bindings / commands / device_status（复用 C1 模型）
- 启动即连本地 MQTT（认证）：日志 `mqtt connected to 127.0.0.1:1883`、`rc=0`
- **官方仓同步增强**：`backend/api/app/config.py` 增加 `MQTT_USERNAME/MQTT_PASSWORD`（空则匿名，兼容云端内网明文）；`mqtt_client.py` connect 时按配置带凭据

### 1.3 端到端验证（已验收）

#### 阶段一：本机链路（homemind-api → 本地 MQTT → SQLite）
链路：JWT 鉴权 → 绑定设备 → 下发命令 → 本地 MQTT 收到 → SQLite 落库

| 步骤 | 请求 | 结果 |
| --- | --- | --- |
| 建用户+JWT | security.create_tokens | token 208 字符 |
| POST /v1/devices | bind esp32s3-eye | `{"device_id":"esp32s3-eye","name":"HomeMind-EYE"}` |
| POST /v1/devices/esp32s3-eye/commands | led.on ttl=30 | `{"command_id":"3b6d064d…","status":"queued"}` |
| GET /v1/commands/{id} | 查状态 | queued, ttl 30, created_at 2026-09-07T13:52:00 |
| mosquitto_sub device/esp32s3-eye/cmd | 旁路监听 | `{"command_id":"3b6d064d…","action":"led.on","params":{"ttl":30},"ttl":30,"ts":1788789120}` |
| SQLite 查询 | users/bindings/commands | 各 1 行，commands.status=queued |

#### 阶段二：板端真实执行全链路（网关本地 MQTT 桥接）
链路：API 下发 → 本地 MQTT cmd → **网关订阅接收 → 串口执行 → 板子 LED 点亮** → ack/done 回传 → 本地 MQTT → API 状态机更新 → SQLite

旁路监听 `device/esp32s3-eye/+`（home-api 凭据）完整记录（命令 c133cdf6，led.on）：

| 事件 | 载荷（要点） | 含义 |
| --- | --- | --- |
| cmd | `{"command_id":"c133cdf6…","action":"led.on","ttl":30}` | API 经本地 broker 下发 |
| ack #1 | `{"command_id":"c133cdf6…","status":"acked"}` | 网关收到即回（不阻塞 MQTT） |
| ack #2 | `{"command_id":"c133cdf6…","status":"done"}` | **串口执行完成，板子确认 LED 状态** |
| status | `{"online":true,"led":"on",…}` | 网关上报设备在线、LED=on |

最终命令状态：`status=done`，`acked_at` 13:58:51.648833 → `done_at` 13:58:51.697963（49ms 内完成板端确认）。SQLite 同步：commands 表 done；device_status `online=1, led_state='on', last_seen` 更新；此前 3b6d064d 命令被 TTL 过期扫描自动置 expired（expire_sweeper 生效）。

### 1.4 网关双 MQTT 连接（工作包 B 第二步）
- **基线统一**：以官方仓版 `gateway_agent.py`（含 HA 播报重试 ×2）为基线，补齐本地 MQTT 双连接支持（`LOCAL_MQTT_HOST/PORT/USER/PASS` 配置则启用），解决现场版与官方仓版版本漂移。
- 部署：官方仓版（补丁后）复制到现场 `/home/hfy/homemind-gateway/gateway_agent.py`（哈希一致校验通过）；现场运行版备份 `gateway_agent.py.bak.20260907_prelocalmqtt`。
- .env 追加：`LOCAL_MQTT_HOST=127.0.0.1 / LOCAL_MQTT_PORT=1883 / LOCAL_MQTT_USER=home-gateway / LOCAL_MQTT_PASS=…`（云端 MQTT_USER/PASS/TLS 配置保留，双连接并存）。
- 重启后日志：`串口会话已就绪（/dev/ttyACM0 @ 115200）` + `本地 MQTT 已启动：127.0.0.1:1883（用户 home-gateway）` + 云端/本地各 `已连接 MQTT 并订阅`。

### 1.5 家庭业务接口 /v1/intents|events|tasks|assets（工作包 B 第三步）
- **模型**（models.py 新增 4 表）：`intents`（意图记录+白名单拒绝原因）、`home_events`（感知事件：来源/类型/载荷）、`tasks`（待办/日程：到期时间/家庭时区/提醒/完成状态）、`assets`（家庭资产：类别/位置/属性，不整体发送 MiMo）。
- **路由**（routers/ 新增 4 文件，均 JWT 鉴权）：
  - `POST/GET /v1/intents`：创建意图（动作白名单校验，未知动作标记 rejected 并保留 reason）；查询支持 status/limit
  - `POST/GET /v1/events`：创建感知事件（source=vision/voice/sensor/mqtt/manual）；按 device_id/event_type/source 过滤查询
  - `POST/GET/PATCH /v1/tasks`：创建待办（due_at/remind_at 解析、remind≤due 校验、时区默认 Asia/Shanghai）；状态流转 pending→done/cancelled（completed_at 记录）
  - `POST/GET/PATCH /v1/assets`：资产记录与按需更新（attributes 局部更新）
- **验收（2026-09-07 14:23）**：全部通过——意图 pending 创建、`door.unlock` 拒绝（reason=action not allowed）、person_detected 事件落库、待办含提醒创建+流转 done、资产按 category 查询；SQLite 现有 8 表（+assets/commands/device_bindings/device_status/home_events/intents/tasks/users）。
- 部署：官方仓 backend/api/app 实现 → 同步家庭副本 `/home/hfy/homemind-backend/app/` → 重启 homemind-api（active，MQTT rc=0）。

## 2. 部署清单（家庭主机现状，2026-09-07 14:00）

| 服务 | 端口 | 状态 | 数据/配置 |
| --- | --- | --- | --- |
| mosquitto | 0.0.0.0:1883（LAN，密码认证） | active（自启） | /etc/mosquitto/ + /var/lib/mosquitto/ + passwd(home-gateway/home-api) |
| homemind-api | 127.0.0.1:8001 | active（自启） | /home/hfy/homemind-backend/（代码+venv+.env 0600）+ /home/hfy/homemind-data/homemind.db |
| homemind-gateway | （串口 + 云端/本地双 MQTT） | active（自启） | /home/hfy/homemind-gateway/（官方仓版+LOCAL 配置） |
| homeassistant | 8123 | active（原有，docker） | /home/hfy/homeassistant/config/（未改动） |

## 3. 备份（迁移前置，2026-09-07 13:47 完成）

- 目录：`/home/hfy/backups/2026-09-07-migration-pre/`（25 MB）
- 内容：gateway.env.bak（0600）、gateway_agent.py.bak、mihome_adapter.py.bak、command_policy.py.bak、HA-config-snapshot/（sudo 完整复制 config 卷）、home-assistant_v2.db.bak（Python sqlite3 online backup，5.0 MB，一致性备份）

## 4. 边界与后续（未越界）

- 官方仓 `backend/` 的**功能增强已提交**（config.py/mqtt_client.py 的 MQTT 凭据支持、gateway_agent.py 的本地 MQTT 双连接）——均为向后兼容（不配置即原行为）；网关 / HA / 公网入口的**运行状态**均未回退，HA 未动。
- 后续（09-08 起）：语义理解接入（MiMo 必要文本 → /v1/intents，工作包 E 落位）；Mosquitto ACL 收敛（当前认证用户可读写全部 topic，家庭 LAN 可接受）；迁移核对后停公网业务写入并收敛云端连接。
- 已知差异待办：**已解决**——gateway_agent.py 现场版与官方仓版（播报重试）差异已以官方仓版为基线统一，现场运行版备份保留。
