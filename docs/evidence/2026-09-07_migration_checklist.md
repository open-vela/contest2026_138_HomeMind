# 家庭服务迁移核对（工作包 B 收尾，2026-09-07）

> 范围：工作包 B（家庭服务迁移，计划 09-08~09-10）收尾核对。核对对象：家庭主机 192.168.31.251 上 Mosquitto / FastAPI / SQLite / 网关 / 板端闭环。
> 状态口径：已验收 / 部分完成 / 待验证 / 未实现。本核对基于 2026-09-07 14:32 实测。

## 1. 服务与进程核对

| 服务 | 端口 | 状态（重启后） | 说明 |
| --- | --- | --- | --- |
| mosquitto | 0.0.0.0:1883 | active | 认证 + ACL（见 §2），家庭 LAN 内可达 |
| homemind-api | 127.0.0.1:8001 | active | uvicorn + FastAPI，JWT 鉴权，MQTT rc=0 |
| homemind-gateway | — | active | 双 MQTT：云端 api.hfy-ai.cloud:8883 TLS（出站）+ 本地 127.0.0.1:1883 |
| Home Assistant | 0.0.0.0:8123 | active（docker） | 未改动，保持冻结 |

## 2. MQTT ACL 收敛（已验收）

- 配置：`/etc/mosquitto/conf.d/20-homemind-acl.conf` + `/etc/mosquitto/acl`（root:mosquitto 640）
- 权限矩阵（订阅端确认法实测，越权发布被 broker 丢弃）：

| 操作 | home-api | home-gateway |
| --- | --- | --- |
| read `device/esp32s3-eye/cmd` | —（无） | ✓ |
| read `device/esp32s3-eye/speak` | —（无） | ✓ |
| read `device/+/ack` | ✓ | —（仅本设备 ack） |
| read `device/+/status` | ✓ | —（仅本设备 status） |
| write `device/esp32s3-eye/cmd` | ✓ | ✗ 拒绝（实测） |
| write `device/esp32s3-eye/speak` | ✓ | ✗ 拒绝 |
| write `device/esp32s3-eye/ack` | ✗ 拒绝（实测） | ✓ |
| write `device/esp32s3-eye/status` | ✗ 拒绝 | ✓ |
| write `homemind/#` | ✓ | —（无） |

- 坑：ACL 曾用 `%c`（client id）占位，paho 客户端 client id 为随机值导致不匹配；改为显式 device topic 后生效。
- 未实现：设备侧凭据轮换（本期单设备，凭据已存 .env 0600）。

## 3. 数据核对（SQLite 8 表）

`/home/hfy/homemind-data/homemind.db`：

| 表 | 用途 | 核对 |
| --- | --- | --- |
| users | 用户与绑定 | test-home-e2e 留库 |
| device_bindings | 设备归属 | esp32s3-eye 绑定 |
| device_status | 设备在线/状态 | online=1, led_state=on |
| commands | 命令状态机 | queued→acked→done 记录 |
| intents / home_events / tasks / assets | 家庭业务（B3） | 验收行在库 |

## 4. 闭环与接口核对（重启后复测，已验收）

- 全服务重启（mosquitto + homemind-api + homemind-gateway）→ 全部自动恢复，端口就绪。
- 命令闭环：API 下发 `led.on` → 本地 MQTT cmd → 网关串口 execute → 板端 LED 亮 → ack `acked`（14.3ms）→ `done`（49.7ms）→ SQLite 状态 done。命令 id `9de38d05ef8146b5bef2df833235b77c`。
- 业务接口：/v1/intents、/v1/events、/v1/tasks、/v1/assets 均 JWT 鉴权可访问（B3 已验收，见 `2026-09-07_home_backend_e2e.md`）。

## 5. 公网边界核查（部分完成，边界已明确）

- 家庭侧业务写入**全部走本地**：FastAPI 仅连 127.0.0.1:1883；intents/events/tasks/assets 仅存家庭 SQLite。✅
- 云端连接：网关仅**主动出站**（mqtts 8883 TLS），家庭不开放公网入站端口。✅
- 公网业务正文：新业务接口（意图/事件/待办/资产）不在云端落库；云端仅保留登录/设备绑定/命令通道（历史部署）。✅
- 未完成：**云端业务写入停用**依赖"家庭出站通道和入口转发"（C1.0 09-08~10 项，尚未实现）——入口转发验收后，网关云端连接收敛为仅入口、云端不再承担业务状态机，并将处理旧数据库/备份/日志中业务正文的保留问题。此处不提前宣称完成。

## 6. 保留阻塞 / 未实现（如实）

- PCM 多缓冲未修（方向见 `2026-09-07_pcm_multibuffer_diag.md`）。
- 端侧视觉推理、离线唤醒、真机小程序、微信域名登记（request https://hfy-ai.cloud / socket wss://hfy-ai.cloud）未实现/未验收。
- 官方仓未 push（远端仍为模板 commit，既定纪律）。

## 7. 结论

工作包 B（家庭服务迁移）三步 + 收尾核对完成：服务、数据、闭环、重启恢复、MQTT ACL 均已验收；公网边界符合"家庭侧业务不入公网、云端仅远程入口"的方向，入口转发与云端写入停用列为后续工作包明确待办。
