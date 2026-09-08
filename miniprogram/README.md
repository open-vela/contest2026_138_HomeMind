# HomeMind 微信小程序（Win11 专属）

本目录继续在 Win11 的微信开发者工具中开发，不进入默认 Ubuntu Core 迁移包。

## 当前状态（更新 2026-09-07；事实截止 2026-09-06）

**部分完成**：模拟器登录、真实灯光控制与实体自动同步已有[物理闭环记录](../docs/evidence/2026-09-06_miniprogram_mihome_physical.md)和[实体同步记录](../docs/evidence/2026-09-06_mihome_entity_sync.md)。当前多功能房吸顶灯的小程序全链路、手机真机、弱网与权限仍待验证；不再将模拟器登录或米家登录列为未完成。

日程仅存微信本地 Storage。目标为家庭 SQLite 保存事件、待办/日程和资产，小程序跨端同步；家庭业务迁移与模型尚未实现。腾讯云保留远程访问入口且不持久化业务正文是待验收目标，远程数据仍经过公网入口。见 [STATUS](../STATUS.md) 和[家庭服务迁移计划](../docs/C1.0-腾讯云资产与后端计划.md)。

## 现有设备接口与迁移方向

- 正式链路：小程序 → 腾讯云 API（HTTPS/WSS）→ 家庭 Ubuntu 主动出站 → 设备；
- `app.globalData.serverUrl` 默认为 `https://hfy-ai.cloud`，WSS 地址从该 HTTPS 地址派生；
  设置页仅接受 HTTPS 地址，合法域名登记已有用户记录，仍需手机回归；
- 小程序侧不持有 AppSecret、证书私钥、设备主密钥或 MiMo Token；仅在微信 Storage
  保存登录后下发的 access token，未使用的 refresh token 不落盘；
- **已接入云端**：
  - `utils/api.js`：微信登录(`/v1/auth/wechat/login`)、设备列表(`GET /v1/devices`)、下发(`POST /v1/devices/{id}/commands`)、查状态(`GET /v1/commands/{id}`)；
  - `utils/ws.js`：WSS `/v1/ws/app?token=...` 推送状态与命令 ACK（心跳 20s）；
  - `pages/devices`：登录后自动绑定 `esp32s3-eye`，开灯/关灯经云端下发；只有收到最终 `done/expired` 才解除防重入，WSS 丢失时轮询命令状态，并支持真实云端解绑；
  - `pages/settings`：新增微信登录状态卡片，服务器地址默认 `hfy-ai.cloud`；
- 后端实现见 `../backend/`（nginx + FastAPI + mosquitto TLS + SQLite），部署手册 `../backend/README.md`。
- 设备侧已就绪能力：自动联网、MiMo 问答、本地工具（LED/设备信息/BOOT 键）。

## 已知限制（不使用假数据）

- 未接入温度、湿度、光照和噪声传感器；页面统一显示 `--` / `未接入`；
- 告警服务、消息通知和隐私模式开关尚未接入；
- 日程当前仅保存在微信本地 Storage，不宣称已开启云端提醒；
- 设备在线数仅在后端 `GET /v1/devices` 成功返回时展示，请求失败时显示“服务不可用”。

## 待验收项

- 当前多功能房吸顶灯小程序全链路，开/关各 10 次并回读真实状态。
- 手机真机登录、绑定、控制、状态回读、断线重连、弱网及权限。
- 家庭服务迁移后的 API/WSS、事件/待办/资产同步与服务重启持久化。
- “我准备睡觉了，明早八点提醒我带钥匙”产生受校验任务，设备和待办结果分别展示。

## 备份入口

使用官方工作副本中的备份工具，参见[工作区说明](../../README_WORKSPACE.md)。旧普通副本已经归档，不再作为当前入口。
