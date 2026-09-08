# MiHome 网关部署记录 — 2026-09-06

性质：部署与勘察记录。真实米家设备 `mihome.set_power` / `mihome.get_state` 验收**尚未执行**，不能依据本文件宣称设备闭环完成。

## 本轮完成

- 服务器 `192.168.31.251` 勘察（SSH hfy@）：
  - Home Assistant 以 Docker 容器 `homeassistant`（stable）运行，已运行 2 天，宿主机 `127.0.0.1:8123` 可达；未带 token 的 `GET /api/` 返回 401（鉴权正常）。
  - `.storage/core.entity_registry` 中 `xiaomi_home` 平台实体 1835 个，其中 `light.*` 51 个、`switch.*` 362 个（仅读取实体 ID 与名称，未读取或输出任何凭据）。米家集成已导入真实设备，与用户 2026-09-06 确认的“米家登录已解除”一致。
  - `homemind-gateway.service` 此前运行 2026-09-02 版 `gateway_agent.py`（无 mihome 分支），且 `mihome_adapter.py` 未部署。
- 部署（备份后覆盖，备份后缀 `.bak.20260906_*`）：
  - `gateway_agent.py`（含 mihome 命令分支）、`mihome_adapter.py`、`command_policy.py` 上传至 `/home/hfy/homemind-gateway/`；部署后 SHA-256 与仓库 `contest2026_138_HomeMind_official/backend/gateway-agent/` 一致（gw `840eaf0c…`，adapter `3e923e38…`）。
  - 上传前 `py_compile` 通过；部署后 `systemctl restart homemind-gateway` 成功，服务 `active`，串口会话就绪（`/dev/ttyACM0 @ 115200`），MQTT 已连接并订阅 `device/esp32s3-eye/cmd`。
  - `.env` 追加 `HOMEMIND_HA_URL=http://127.0.0.1:8123`、`HOMEMIND_HA_TOKEN=`（空）、`HOMEMIND_MIHOME_ALLOWED_ENTITIES=`（空）。
- 单元测试：`tests/test_mihome_adapter.py` 3 项（allowlist 回读、白名单外拒绝、缺配置 fail-closed）在本地全部通过。

## 当前边界（阻塞项）

1. `HOMEMIND_HA_TOKEN` 为空：需要用户在 HA 网页（用户头像 → 安全 → 长期访问令牌）创建长期访问令牌后写入。令牌不进入仓库与日志。
2. `HOMEMIND_MIHOME_ALLOWED_ENTITIES` 为空：待用户从实体清单中指定一台允许开关的 `light.*` 或 `switch.*` 设备。适配器仅在 URL、token、非空白名单齐备时启用，否则 fail-closed。
3. 完成 token + 白名单配置后，验收路径为：MQTT `device/esp32s3-eye/cmd` 发布 `mihome.set_power` → 网关 → HA REST → 真实设备开关 → 状态回读 → `acked → done` 与 `mihome_result`。尚无上述任一段的实机证据。
