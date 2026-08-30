# HomeMind 微信小程序（Win11 专属）

本目录继续在 Win11 的微信开发者工具中开发，不进入默认 Ubuntu Core 迁移包。

## 当前状态（2026-08-29 更新）：可编译就绪

C1 小程序侧基础恢复完成：

1. ✅ 5 个页面脚本（index/dashboard/devices/calendar/settings）的语法损坏全部修复
   （模板字符串反引号、计算属性键、about 弹窗字符串——均为此前 shell 转义损伤），
   全部通过 esprima JS 语法解析；
2. ✅ `sitemap.json` 已补齐；tabBar 的 10 个图标（81x81 PNG，普通/选中两色）
   已生成到 `images/`；
3. ✅ `app.js` 已删除硬编码局域网地址（`192.168.1.100`），服务地址改为空 +
   从设置页保存的 Storage 恢复；
4. ✅ 页面四件套（js/json/wxml/wxss）齐全，所有 JSON 校验通过；
5. ⏳ `project.config.json` 已填入项目 AppID（AppID 是公开标识，不是 AppSecret），
   仍需在微信开发者工具完成首次编译和真机验收。

## C1 对接预留（按总体计划 5.5 节）

- 正式链路：小程序 → 腾讯云 API（HTTPS/WSS）→ 家庭 Ubuntu 主动出站 → 设备；
- `app.globalData.serverUrl` 默认为 `https://hfy-ai.cloud`，WSS 地址从该 HTTPS 地址派生；
  设置页仅接受 HTTPS 地址，真机验收前还需确认微信后台合法域名配置；
- 小程序侧不持有 AppSecret、证书私钥、设备主密钥或 MiMo Token；仅在微信 Storage
  保存登录后下发的 access token，未使用的 refresh token 不落盘；
- **已接入云端**：
  - `utils/api.js`：微信登录(`/v1/auth/wechat/login`)、设备列表(`GET /v1/devices`)、下发(`POST /v1/devices/{id}/commands`)、查状态(`GET /v1/commands/{id}`)；
  - `utils/ws.js`：WSS `/v1/ws/app?token=...` 推送状态与命令 ACK（心跳 20s）；
  - `pages/devices`：登录后自动绑定 `esp32s3-eye`，开灯/关灯经云端下发，WSS 收到 ACK 后页面更新；
  - `pages/settings`：新增微信登录状态卡片，服务器地址默认 `hfy-ai.cloud`；
- 后端实现见 `../backend/`（nginx + FastAPI + mosquitto TLS + SQLite），部署手册 `../backend/README.md`。
- 设备侧已就绪能力：自动联网、MiMo 问答、本地工具（LED/设备信息/BOOT 键）。

## 已知限制（不使用假数据）

- 未接入温度、湿度、光照和噪声传感器；页面统一显示 `--` / `未接入`；
- 告警服务、消息通知和隐私模式开关尚未接入；
- 日程当前仅保存在微信本地 Storage，不宣称已开启云端提醒；
- 设备在线数仅在后端 `GET /v1/devices` 成功返回时展示，请求失败时显示“服务不可用”。

## 待验收项

- 微信开发者工具清洁编译；
- 真机 HTTPS 登录、WSS 状态推送与 LED 命令 ACK。

## 备份命令

```powershell
cd C:\Old\HomeMind
.\contest2026_138_HomeMind\tools\create-win11-backup.ps1
```
