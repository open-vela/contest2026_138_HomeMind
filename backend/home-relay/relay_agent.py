#!/usr/bin/env python3
"""HomeMind 家庭出站通道（relay）——部署在家庭 Ubuntu，只主动出站。

职责：订阅云端 broker 的 homemind/relay/req，把业务请求转发到家庭 FastAPI
（127.0.0.1:8001，Authorization 透传，家庭与云端共用 JWT_SECRET），再把响应
发布回 homemind/relay/resp。云端 API 只做入口（微信登录/转发），业务正文
（intents/events/tasks/assets/commands）全部落在家庭 SQLite。

安全边界：
- 只出站连接（生产 mqtts://api.hfy-ai.cloud:8883 TLS），家庭不开放任何公网入站端口；
- req_id 回显校验，resp 仅携带与 req_id 对应的结果；
- 断线指数退避重连；不缓存任何请求正文。

测试模式：RELAY_MQTT_HOST=127.0.0.1 RELAY_MQTT_PORT=1883 RELAY_TLS=false
连接本地 mosquitto（用户 home-relay），用 mosquitto_pub/sub 模拟云端入口。
"""
import os
import sys
import time
import json
import logging
import threading
import urllib.request
import urllib.error

import paho.mqtt.client as mqtt

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
logger = logging.getLogger("home-relay")


def _load_env(path=None):
    if path is None:
        path = os.path.join(os.path.dirname(os.path.abspath(__file__)), ".env")
    if not os.path.exists(path):
        return
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            k, v = line.split("=", 1)
            os.environ.setdefault(k.strip(), v.strip())


_load_env()

# ── 配置 ──
MQTT_HOST = os.getenv("RELAY_MQTT_HOST", "api.hfy-ai.cloud")
MQTT_PORT = int(os.getenv("RELAY_MQTT_PORT", "8883"))
MQTT_USER = os.getenv("RELAY_MQTT_USER", "")
MQTT_PASS = os.getenv("RELAY_MQTT_PASS", "")
RELAY_TLS = os.getenv("RELAY_TLS", "true").lower() in ("1", "true", "yes")
TLS_CA_PATH = os.getenv("RELAY_TLS_CA_PATH", "")  # 自签证书路径；空=系统 CA
API_BASE = os.getenv("RELAY_API_BASE", "http://127.0.0.1:8001")
TOPIC_REQ = os.getenv("RELAY_TOPIC_REQ", "homemind/relay/req")
TOPIC_RESP = os.getenv("RELAY_TOPIC_RESP", "homemind/relay/resp")
KEEPALIVE = int(os.getenv("RELAY_KEEPALIVE", "60"))
REQ_TIMEOUT = float(os.getenv("RELAY_REQ_TIMEOUT", "20"))


def _forward(payload: dict) -> dict:
    """把 relay 请求转发到家庭 API，返回 resp 载荷。"""
    req_id = payload.get("req_id")
    method = (payload.get("method") or "GET").upper()
    path = payload.get("path") or "/"
    body = payload.get("body")
    auth = payload.get("authorization", "")
    url = API_BASE + path
    headers = {"Content-Type": "application/json"}
    if auth:
        headers["Authorization"] = auth
    data = None
    if body is not None:
        data = json.dumps(body).encode("utf-8")
    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=REQ_TIMEOUT) as resp:
            raw = resp.read().decode("utf-8", errors="replace")
            try:
                parsed = json.loads(raw)
            except Exception:
                parsed = raw
            return {"req_id": req_id, "status": resp.status,
                    "body": parsed, "error": None}
    except urllib.error.HTTPError as e:
        raw = e.read().decode("utf-8", errors="replace")
        try:
            parsed = json.loads(raw)
        except Exception:
            parsed = raw
        return {"req_id": req_id, "status": e.code,
                "body": parsed, "error": None}
    except Exception as e:
        return {"req_id": req_id, "status": 502,
                "body": None, "error": f"{type(e).__name__}: {e}"}


def main():
    client = mqtt.Client()
    client.username_pw_set(MQTT_USER, MQTT_PASS)
    if RELAY_TLS:
        if TLS_CA_PATH:
            client.tls_set(ca_certs=TLS_CA_PATH, cert_reqs=mqtt.ssl.CERT_REQUIRED)
        else:
            client.tls_set(cert_reqs=mqtt.ssl.CERT_REQUIRED)  # 系统 CA（Let's Encrypt）

    backoff = 2

    def on_connect(cli, userdata, flags, rc):
        nonlocal backoff
        if rc == 0:
            backoff = 2
            cli.subscribe(TOPIC_REQ)
            logger.info("relay 已连接 %s:%s 并订阅 %s", MQTT_HOST, MQTT_PORT, TOPIC_REQ)
        else:
            logger.error("relay MQTT 连接失败 rc=%s", rc)

    def on_message(cli, userdata, msg):
        if msg.topic != TOPIC_REQ:
            return
        try:
            payload = json.loads(msg.payload.decode())
        except Exception as e:
            logger.warning("非法 relay 请求：%s", e)
            return
        req_id = payload.get("req_id") if isinstance(payload, dict) else None
        if not isinstance(req_id, str) or not req_id:
            logger.warning("relay 请求缺少 req_id，丢弃")
            return
        logger.info("转发请求 %s %s %s", req_id, payload.get("method"), payload.get("path"))
        resp = _forward(payload)
        cli.publish(TOPIC_RESP, json.dumps(resp, ensure_ascii=False), qos=1)
        logger.info("已回传 %s status=%s", req_id, resp.get("status"))

    client.on_connect = on_connect
    client.on_message = on_message

    while True:
        try:
            client.connect(MQTT_HOST, MQTT_PORT, KEEPALIVE)
            client.loop_forever(retry_first_connection=True)
        except Exception as e:
            logger.error("relay MQTT 循环异常：%s，%ss 后重连", e, backoff)
            time.sleep(backoff)
            backoff = min(backoff * 2, 60)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(0)
