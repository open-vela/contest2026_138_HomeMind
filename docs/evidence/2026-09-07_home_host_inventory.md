# 家庭主机资产盘点与来源映射（2026-09-07，工作包 B 准备）

> 目的：迁移计划（docs/C1.0-腾讯云资产与后端计划.md）09-07 交付物——盘点当前容器/版本/配置/数据库位置，建立备份与来源映射，明确迁移/回退范围，不覆盖唯一可用版本。
> 盘点主机：Ubuntu 22.04 @ 192.168.31.251（用户 hfy），时间 2026-09-07。

## 1. 系统基线

| 项 | 值 |
| --- | --- |
| 系统 | Ubuntu 22.04（用户本地家庭常开主机） |
| 磁盘 | / 231G，已用 19G（9%），可用 201G |
| 内存 | 7.6 GiB，used 698 MiB，可用 6.6 GiB |
| Docker | daemon active（容器/镜像见下） |

## 2. 现有服务与资产

### 2.1 家庭网关（systemd：homemind-gateway.service，active）

- 工作目录：`/home/hfy/homemind-gateway/`
- 服务定义：`/etc/systemd/system/homemind-gateway.service`（Type=simple，User=hfy，Restart=always，RestartSec=3，WorkingDirectory 如上，ExecStart=`venv/bin/python -u gateway_agent.py`）
- 运行进程：`venv/bin/python -u gateway_agent.py`（PID 923）
- Python：venv Python 3.10.12；`paho-mqtt 1.6.1`、`pyserial 3.5`（requirements.txt 同目录）
- 代码文件（源码来源=Ubuntu 官方仓 `contest-final`，见 STATUS 2026-09-06 提交记录）：
  - `gateway_agent.py`（11017 B，2026-09-06 11:59，含 mihome 分支）
  - `mihome_adapter.py`（9628 B，2026-09-06 11:59）
  - `command_policy.py`（3338 B，2026-09-06 07:48）
  - 迭代备份：`*.bak.20260902`、`*.bak.20260906_154801`、`mihome_adapter.py.bak.20260906_e2e`（保留不覆盖）
- **版本差异（2026-09-07 盘点发现，待工作包 B 统一）**：现场运行文件与官方仓 `backend/gateway-agent/gateway_agent.py`（11368 B，2026-09-06 12:07）哈希不一致；差异为播报分支——仓版带"HA 首次调用新 notify 实体超时重试一次（共 2 次尝试）"，现场版无重试。`mihome_adapter.py`/`command_policy.py` 两处哈希一致。另：现场文件 mtime（11:59）晚于当前网关进程启动（09-07 11:56），运行中进程实际加载的字节版本未实测，迁移时以"官方仓版 + 现场 .env"为基线并重启服务确认。
- 凭据：`.env`（权限 600，仅 hfy 可读；键名见 §3，值不落库/不进仓库）
- 串口：/dev/ttyACM0（ESP32-S3-EYE）

### 2.2 Home Assistant（Docker 容器 `homeassistant`，Up 21 小时）

- 镜像：`docker.m.daocloud.io/homeassistant/home-assistant:stable`（另有 `ghcr.nju.edu.cn/home-assistant/home-assistant:stable` 同尺寸镜像并存；hello-world 为拉取测试残留）
- 镜像体积：3.43 GB
- 容器配置：`Restart=unless-stopped`；卷 `config=/home/hfy/homeassistant/config` 与 `/etc/localtime` 只读挂载；无 docker-compose 文件（单容器 docker run 部署）
- 版本：`2026.9.0`（config/.HA_VERSION）
- 端口：0.0.0.0:8123
- 数据：`config/home-assistant_v2.db`（5.0 MB）+ `-wal`（4.1 MB）+ `-shm`；日志 `home-assistant.log` / `.log.1`；配置 `configuration.yaml`、`automations.yaml`、`custom_components/`（米家集成等）
- 集成：米家（xiaomi_home）实体 1835 个（2026-09-06 记录），当前白名单仅多功能房吸顶灯

### 2.3 MQTT / FastAPI / SQLite（家庭侧）

- Mosquitto：**未安装**（systemctl inactive，无 mosquitto 二进制）→ 工作包 B 待装
- 家庭 FastAPI：**无**（仅云端腾讯云有 FastAPI 入口；家庭侧只有网关 agent 进程）→ 工作包 B 待建
- 家庭业务 SQLite：**无**（仅有 HA 的 home-assistant_v2.db）→ 工作包 B 待建
- 板端 MQTT：**未接入**（当前网关经串口 /dev/ttyACM0 与 ai_agent 会话通信；板端 MQTT 通道为工作包 B 待办）

## 3. 网关 .env 键名清单（仅键名，值保密）

```
API_HOST / API_PORT / MQTT_USER / MQTT_PASS / TLS_CA_PATH / DEVICE_ID /
SERIAL_PORT / BAUD / CMD_TTL / CMD_MODE /
HOMEMIND_HA_URL / HOMEMIND_HA_TOKEN / HOMEMIND_MIHOME_ALLOWED_ENTITIES /
HOMEMIND_SPEAK_ENTITY
```
- `API_HOST` 指向腾讯云公网入口（历史 C1 部署）；工作包 B 将新增家庭本地 MQTT/FastAPI 目标配置，旧键保留以支持回退。
- `HOMEMIND_HA_TOKEN` 为 HA 长期访问令牌（只存 .env，0600），不进入仓库/日志/本文档。

## 4. 备份与来源映射

| 资产 | 来源 | 备份/回退方式 |
| --- | --- | --- |
| 网关代码（3 个 py + requirements） | Ubuntu 官方仓 `contest-final`（本地提交） | 迭代 .bak 保留；仓库提交可回溯；回退=停服务换回 .bak |
| 网关 .env | 手动配置（含云端/HA 凭据） | 仅存 /home/hfy/homemind-gateway/.env（0600）；迁移前先复制 .env.bak.20260907 再改 |
| HA 容器 | Docker stable 镜像 | `docker inspect` 记录如上；数据在 config/ 卷，先 `cp -a` 到 /home/hfy/backups/HA-20260907/ 再动配置 |
| HA 数据库 | HA 运行生成 | WAL 模式；备份需停容器或 sqlite3 .backup；迁移前执行 |
| 板端固件 | Ubuntu 官方仓 artifacts（c8a373e6…） | 仓库归档；烧录可复现 |
| 公网入口（腾讯云） | 云端部署（历史） | 暂不动；家庭侧完成后按迁移计划核对停用业务写入 |

## 5. 迁移/回退范围（明确）

- **范围内（工作包 B）**：安装 Mosquitto；搭建家庭 FastAPI + SQLite；网关新增本地 MQTT 目标与板端 MQTT 通道；配置/迁移/回退说明文档。
- **范围外**：不改动 HA 容器与数据（除非验收需要）；不删除公网入口（迁移核对后单独处理）；不覆盖网关 .env 现有键（追加本地配置键，保留旧键）。
- **回退**：恢复 .env.bak、停家庭新服务、网关回退串口直连模式，即回到当前已验收状态。

## 6. 操作纪律（本次盘点遵守）

- 只读盘点，未修改任何服务配置、未删除任何文件、未覆盖唯一可用版本。
- 网关/HA 均保持运行，未重启。
- 下一步（09-08 起）：按以上映射先备份（网关 .env + HA config 卷 + HA DB），再安装/配置 Mosquitto 与家庭 FastAPI/SQLite。
