# 米家实体自动同步（网关 → 云端 → 小程序）实施记录 — 2026-09-06

## 动机

用户指出设备页要求手填实体 ID 不合理，且应显示"阳台开关"等友好名称。本轮把实体发现做成自动同步：白名单实体的名称与状态由网关从 Home Assistant 读取后自动下发，手填仅作网关离线时的兜底。

## 已完成并实测（网关侧）

- `mihome_adapter.py` 新增 `list_entities()`（逐白名单实体查 `/api/states`，返回 `entity_id/name/state`）与 `_clean_name()`（小米集成 friendly_name 形如"阳台开关 开关 开关"，去相邻重复 token 与域重复尾部词后得"阳台开关"）。
- `gateway_agent.py` 的 status 消息（连接时与命令完成后）新增 `mihome_entities` 字段。
- 已部署至 `/home/hfy/homemind-gateway/`（py_compile 通过，服务 active）。
- **实测**：发布 `verify-entity-sync-0906`（`mihome.get_state`）→ `acked → done`；status 消息实测为：
  `mihome_entities=[{"entity_id": "switch.linp_cn_950233435_t2dbw1_on_p_2_1", "name": "阳台开关", "state": "on"}]`

## 已完成（云端代码，待 workbuddy 部署）

- `backend/api/app/models.py`：`DeviceStatus.mihome_entities` TEXT 列；
- `backend/api/app/db.py` + `main.py`：`ensure_sqlite_columns()` 轻量迁移（ALTER TABLE 补列，兼容存量行）；
- `backend/api/app/mqtt_client.py`：status 快照存库并经 WSS 透传；
- `backend/api/app/routers/devices.py`：`GET /v1/devices` 返回 `mihome_entities`。
- 部署步骤与验收见 `docs/2026-09-06_腾讯云workbuddy操作清单2-米家实体同步部署.md`。

## 已完成（小程序，待云端部署后实测）

- 设备页"米家设备控制"卡片改为自动渲染实体行：显示名称（阳台开关）+ 当前状态（开/关）+ 开/关/查状态按钮；pending/result 按 `deviceId|entityId` 键控；命令确认后本地同步实体状态，正式快照以网关 status 为准。
- 网关离线或旧网关无快照时回退为手动输入实体 ID。

## 边界

- 云端未部署前，`GET /v1/devices` 不含 `mihome_entities` → 小程序走手动兜底，功能不受损。
- 实体状态为查询时刻快照，设备被他方（HA/手动）操作后的即时性依赖网关 status 上报频率。
