# 腾讯云服务器操作清单（交 workbuddy 执行）— 2026-09-06

目标：为微信小程序真机闭环（wx.login → 绑定 → 命令 → WSS 回显）做云端收尾。
现状（2026-09-06 已从公网验证，无需重复排查）：

- `https://hfy-ai.cloud/v1/health` → `{"status":"ok","service":"homemind-c1"}`；
- `POST /v1/auth/wechat/login`（假 code）→ `401 wechat auth failed: invalid code`，**证明云端已带有效 AppID/AppSecret 调通微信 jscode2session**；
- `api.hfy-ai.cloud` 虽解析到同一 IP（106.53.25.19）但 TLS 握手失败（证书不覆盖该子域）；小程序与微信后台均使用主域 `hfy-ai.cloud`，**不需要**为子域配证书。

## 操作项（按顺序）

### 1. 状态与配置核对（只读，先做）

```bash
ssh <腾讯云主机>
cd <backend 部署目录，含 docker-compose.yml>
docker compose ps                          # nginx/api/mqtt 三容器应全部 Up
grep -E '^WX_APPID=' .env                  # 应输出 WX_APPID=wx75312eb43495775f
grep -E '^(WX_APPSECRET|JWT_SECRET|MQTT_PASS)=' .env | cut -d= -f1   # 只确认键存在，绝不回显值
grep -E '^ALLOWED_ACTIONS=' .env || echo "未设置（用 compose 默认）"
```

### 2. 命令白名单加入 mihome 动作

`docker-compose.yml` 里 `ALLOWED_ACTIONS` 的默认值是 `led.on,led.off,device.info`，**不含 mihome**。在 `.env` 中追加/修改为：

```
ALLOWED_ACTIONS=led.on,led.off,device.info,mihome.set_power,mihome.get_state
```

然后只重建 api 容器（不要动 mqtt/nginx）：

```bash
docker compose up -d --force-recreate api
```

注意：**绝对不要修改或重新生成 MQTT_PASS**——家庭 Ubuntu 网关正在用它连接 8883，改了会断链（deploy.sh 只在缺失时生成）。

### 3. 重启后验证（从云主机或外部均可）

```bash
curl -s -X POST https://hfy-ai.cloud/v1/auth/wechat/login \
  -H 'content-type: application/json' -d '{"code":"probe-fake-0906"}'
# 期望：401 {"detail":"wechat auth failed: invalid code, rid: ..."}
# 若出现 502 或 appid/appsecret missing 类 errmsg，说明微信凭据被改坏，立即回滚 .env 并报告
curl -s https://hfy-ai.cloud/v1/health      # 期望 {"status":"ok",...}
```

### 4. 证书与安全例行检查（只读为主）

```bash
ls -l <部署目录>/certs/fullchain.pem                      # 记录签发日期，判断是否临近到期
openssl x509 -in <部署目录>/certs/fullchain.pem -noout -enddate -subject
systemctl list-timers 2>/dev/null | grep -i cert          # 确认 Let's Encrypt 续期定时任务存在
stat -c '%a' <部署目录>/.env                              # 应为 600
ss -tlnp | grep -E ':(80|443|8883)\b'                     # 对外只应有这三个端口
```

腾讯云控制台安全组核对：入站规则只放行 80/443/8883（TCP），确认没有 22 以外的新增公网端口暴露（SSH 22 如对公网开放，建议改为仅管理机/特定来源 IP）。

### 5. 数据库备份

```bash
cp <部署目录>/api/data/homemind.db <部署目录>/api/data/homemind.db.bak.$(date +%Y%m%d_%H%M%S)
```

## 红线

- `.env` 的 WX_APPSECRET / JWT_SECRET / MQTT_PASS 值不得输出到日志、聊天或工单；
- 不新增公网入站端口；不修改 mosquitto 密码文件；
- 操作前后 `docker compose ps` 与 `/v1/health` 结果留档回报。

## 无需做的（避免范围扩张）

- 不需要为 `api.hfy-ai.cloud` 子域签发/配置证书（当前链路全部走主域）；
- 不需要变更 Nginx 配置；不需要迁移数据库；不需要动家庭 Ubuntu 侧任何东西。
