# HomeMind C1 云端后端 · 部署手册

> 2026-09-07 当前边界：现有后端属于联网原型，严格家庭私有化与业务闭环尚未实现。比赛目标将 FastAPI 业务、SQLite、Mosquitto、转写与编排迁至家庭主机；腾讯云保留远程入口且不持久化业务正文，远程小程序数据仍经过该入口。原始音视频限板端/家庭局域网，历史公网媒体路径须在正式隐私模式关闭。迁移执行以[家庭服务计划](../docs/C1.0-腾讯云资产与后端计划.md)为准，以下现有部署说明不构成目标隐私模式验收。


> 对应文档：`docs/C1.0-腾讯云资产与后端计划.md`
> 本目录为 C1 的**重新实现**（旧 `server/` 骨架已冻结，不直接上线）。

## 架构

```
微信小程序(HTTPS/WSS, 只连微信后台登记的 hfy-ai.cloud)
   │
nginx(:443 TLS 终结) ── /        → api(FastAPI)
   │                   ── /v1/ws → api(WebSocket)
   └── :8883 直通 ──────────────→ mosquitto(TLS, 8883)
                                        ▲ 家庭 Ubuntu gateway-agent 主动出站
                                        │
                              ESP32-S3-EYE(/dev/ttyACM0)
```

安全红线（违反即返工，见 C1.0 §二）：
1. 小程序不持有 AppSecret/证书私钥/设备主密钥/MiMo Token；
2. 生产不开「跳过域名校验」，不用裸 IP/localhost/自签；
3. 家庭 Ubuntu 只出站，不开放公网入站端口；
4. 命令状态机 queued→acked→done/expired，云端不把 queued 当已执行；
5. 高风险动作本期仅留接口（白名单：`led.on/led.off/device.info`）。

## 目录结构

```
backend/
  docker-compose.yml      # 四个服务：nginx / api / mqtt
  .env.example            # 环境变量模板（.env 绝不进仓库）
  deploy.sh               # 一键部署（证书 + MQTT 密码 + compose up + 验收）
  nginx/nginx.conf        # TLS 终结 + WS Upgrade + HSTS
  mosquitto/mosquitto.conf# 内部 1883 明文 / 外部 8883 TLS+密码
  api/                    # FastAPI 应用
  gateway-agent/          # 家庭 Ubuntu 网关（Stage 三）
```

## 阶段一产物（资产确认，见 C1.0 执行记录）

- 云主机：腾讯云广州轻量应用服务器（2C2G, Ubuntu 24.04 + Docker 29）
- 域名：`hfy-ai.cloud`（根域，A 记录 `@ →` 云主机公网 IP；证书 SAN 仅覆盖根域，故 API 端点直接用根域而非 api. 子域）
- 证书：腾讯云 TrustAsia DV（Nginx 格式，覆盖 `hfy-ai.cloud`+`www.hfy-ai.cloud`，有效期至 **2026-11-03**）；部署已预置，真机可信
- ICP 备案：广州为大陆地域，必须备案（已提交/进行中）
- 小程序 AppID/AppSecret：AppSecret 仅写入云端 `.env`，不进仓库/聊天

## 部署步骤（阶段二出口）

### 1) 系统准备（云主机）
```bash
apt update && apt install -y docker.io docker-compose-plugin
```
安全组：放行 `22`（限源 IP）、`443`、`8883`；`80` 仅跳转。

### 2) DNS + 备案
- 腾讯云 DNS 添加 `A` 记录 `@`（根域 `hfy-ai.cloud`）→ 云主机公网 IP（api. 子域未使用）
- 在腾讯云提交 `hfy-ai.cloud` 的 ICP 备案（约 1–3 周）

### 3) 配置环境变量
```bash
cd backend
cp .env.example .env
# 编辑 .env：填 WX_APPID、WX_APPSECRET、JWT_SECRET(openssl rand -hex 32)
```

### 4) 部署
```bash
chmod +x deploy.sh
./deploy.sh            # 生成证书 + mosquitto 密码 + compose up + health 验收
```
`deploy.sh` 只提示网关用户名，不会把 MQTT 密码写到终端日志。部署者应在云主机上通过权限受控的 `.env` 读取密码，并使用安全通道写入家庭网关的 `.env`；禁止复制到聊天、工单或演示日志。

### 5) 微信后台域名（阶段一 #6）
小程序后台 → 开发管理 → 服务器域名：
- request 合法域名：`https://hfy-ai.cloud`
- socket 合法域名：`wss://hfy-ai.cloud`

## 阶段三：家庭网关（家庭 Ubuntu 192.168.31.251）
```bash
cd backend/gateway-agent
python3 -m venv .venv && . .venv/bin/activate
pip install -r requirements.txt
cp .env.example .env     # 通过安全通道填入云端 .env 中生成的 MQTT_PASS
chmod 600 .env
python3 gateway_agent.py # 常驻（建议 nohup / systemd）
```

## 阶段四：小程序真机联通
- 微信开发者工具关闭「跳过域名校验」；
- 真机：登录 → 设备页自动绑定 → 开灯/关灯 → WSS 收到 ACK → 页面更新。

## 验收（阶段二出口 + 总门 C）

```bash
# 健康检查（证书链完整）
curl -sSf https://hfy-ai.cloud/v1/health

# 模拟小程序登录 → 下发命令 → 查状态（需先填 WX_APPID/SECRET）
TOKEN=$(curl -s -X POST https://hfy-ai.cloud/v1/auth/wechat/login \
  -H 'content-type: application/json' -d '{"code":"<wx.login返回的code>"}' \
  | python3 -c 'import sys,json;print(json.load(sys.stdin)["access_token"])')
CID=$(curl -s -X POST https://hfy-ai.cloud/v1/devices/esp32s3-eye/commands \
  -H "Authorization: Bearer $TOKEN" -H 'content-type: application/json' \
  -d '{"action":"led.on"}' | python3 -c 'import sys,json;print(json.load(sys.stdin)["command_id"])')
curl -s https://hfy-ai.cloud/v1/commands/$CID -H "Authorization: Bearer $TOKEN"
# 期望网关执行后状态流转 queued → acked → done
```

安全红线核对：
- [ ] 未携带 token 一律 401
- [ ] 伪造 command_id 无法越权读取他人命令
- [ ] 网关只出站，云主机无设备侧公网入站端口
- [ ] 证书为正式/受信任，无自签用于真机
- [ ] .env / password.txt / certs 均未进仓库
