#!/usr/bin/env bash
# HomeMind C1 部署脚本（在腾讯云主机上执行）
# 前置：docker + docker compose 插件；已存在 .env（由 .env.example 复制填写）
set -euo pipefail

cd "$(dirname "$0")"

if [ ! -f .env ]; then
  echo "✗ 缺少 .env，请先 cp .env.example .env 并填写 WX_APPID/WX_APPSECRET/JWT_SECRET"
  exit 1
fi
chmod 600 .env

for required_var in WX_APPID WX_APPSECRET JWT_SECRET; do
  required_value=$(grep -E "^${required_var}=" .env | tail -n 1 | cut -d= -f2- || true)
  if [ -z "$required_value" ] || [[ "$required_value" == your-* ]] || [[ "$required_value" == CHANGE_ME* ]]; then
    echo "✗ ${required_var} 缺失或仍为占位值"
    exit 1
  fi
done
JWT_VALUE=$(grep -E '^JWT_SECRET=' .env | tail -n 1 | cut -d= -f2-)
if [ "${#JWT_VALUE}" -lt 32 ]; then
  echo "✗ JWT_SECRET 长度不足 32 字符"
  exit 1
fi
unset required_value JWT_VALUE

# 1) JWT 密钥校验
if grep -q "CHANGE_ME_GENERATE_RANDOM" .env; then
  echo "✗ JWT_SECRET 仍为占位值，请执行：openssl rand -hex 32 后填入 .env"
  exit 1
fi

# 2) 证书
mkdir -p certs
if [ ! -f certs/fullchain.pem ] || [ ! -f certs/privkey.pem ]; then
  if [ "${LETSENCRYPT:-0}" = "1" ] && [ -d /etc/letsencrypt/live/hfy-ai.cloud ]; then
    echo "→ 使用 Let's Encrypt 证书"
    cp /etc/letsencrypt/live/hfy-ai.cloud/fullchain.pem certs/fullchain.pem
    cp /etc/letsencrypt/live/hfy-ai.cloud/privkey.pem  certs/privkey.pem
  elif [ "${ALLOW_SELF_SIGNED:-0}" = "1" ]; then
    echo "→ 生成自签证书（仅局域网/开发联调；真机出口门C前请换真实证书）"
    openssl req -x509 -newkey rsa:2048 -nodes \
      -keyout certs/privkey.pem -out certs/fullchain.pem -days 365 \
      -subj "/CN=hfy-ai.cloud"
  else
    echo "✗ 未找到正式证书；生产部署拒绝自动生成自签证书（开发联调可显式设置 ALLOW_SELF_SIGNED=1）"
    exit 1
  fi
fi
chmod 600 certs/privkey.pem
# mosquitto 容器内以 uid 1883 运行，必须把证书属主改为 1883 才能读取
echo "→ 修正证书属主为 mosquitto(uid 1883)"
chown -R 1883:1883 certs 2>/dev/null || sudo chown -R 1883:1883 certs
chmod 644 certs/fullchain.pem
chmod 600 certs/privkey.pem

# 3) MQTT 密码文件（网关用户 gateway）
# MQTT_PASS 以 .env 为真相源：已存在则复用（重部署可恢复），否则随机生成
if ! grep -q '^MQTT_PASS=' .env 2>/dev/null; then
  echo "MQTT_PASS=$(openssl rand -hex 16)" >> .env
fi
GWPASS=$(grep '^MQTT_PASS=' .env | cut -d= -f2-)
rm -f mosquitto/password.txt
echo "→ 生成 mosquitto 密码文件（用户 gateway）"
docker run --rm --user "$(id -u):$(id -g)" -v "$PWD/mosquitto:/mqrt" eclipse-mosquitto:2 \
  mosquitto_passwd -c -b /mqrt/password.txt gateway "$GWPASS"
# 容器内 mosquitto(uid 1883) 必须能读 password.txt；属主改 1883，权限 600
chown 1883:1883 mosquitto/password.txt 2>/dev/null || sudo chown 1883:1883 mosquitto/password.txt
chmod 600 mosquitto/password.txt
unset GWPASS
echo "============================================"
echo "家庭网关凭据（写入 home Ubuntu 的 gateway-agent/.env）："
echo "  MQTT_USER=gateway"
echo "  MQTT_PASS=<请从权限为 600 的服务端 .env 安全读取，禁止复制到日志/聊天>"
echo "============================================"

# 4) 构建并启动
echo "→ docker compose build && up -d"
docker compose build
docker compose up -d

# 5) 验收
echo "→ 等待服务就绪..."
sleep 5
if curl -sSf --resolve hfy-ai.cloud:443:127.0.0.1 https://hfy-ai.cloud/v1/health >/dev/null 2>&1; then
  echo "✓ /v1/health OK"
else
  echo "✗ health 检查失败，请 docker compose logs 排查"
  exit 1
fi
echo "✓ 部署完成。下一步：配置微信后台域名 + 启动家庭网关 gateway-agent"
