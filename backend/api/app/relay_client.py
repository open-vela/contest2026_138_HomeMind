"""云端入口转发器：把业务请求经家庭出站通道（relay）转发到家庭 FastAPI。

设计（C1.0）：微信小程序 → 腾讯云入口（微信登录/JWT 签发）→ 本模块
发布 homemind/relay/req → 家庭 relay 订阅并调用家庭 API → 回发
homemind/relay/resp → 本模块同步等待并返回给小程序。业务正文
（intents/events/tasks/assets）只落在家庭 SQLite，云端不落盘。

前提：
- 云端与家庭共用 JWT_SECRET（云端签发的小程序 JWT，家庭 API 可直接校验）；
- RELAY_MODE=true 时业务路由走转发；auth/health 仍由云端本地处理；
- 云端 broker ACL：API 用户可写 homemind/relay/req、读 homemind/relay/resp。
"""
import json
import uuid
import logging
import threading
from .config import settings
from .mqtt_client import client as mqtt_client

logger = logging.getLogger("relay")

TOPIC_REQ = "homemind/relay/req"
TOPIC_RESP = "homemind/relay/resp"

# req_id -> threading.Event 与结果
_pending = {}
_pending_lock = threading.Lock()


def handle_relay_resp(payload: dict):
    """由 mqtt_client.on_message 在收到 homemind/relay/resp 时调用。"""
    req_id = payload.get("req_id") if isinstance(payload, dict) else None
    if not req_id:
        return
    with _pending_lock:
        entry = _pending.pop(req_id, None)
    if entry is None:
        return
    entry["result"] = payload
    entry["event"].set()


def forward(method: str, path: str, body=None, authorization: str = "") -> tuple:
    """发布 relay 请求并同步等待家庭响应。返回 (status, body)。"""
    if mqtt_client is None:
        return 503, {"error": "mqtt not connected"}
    req_id = uuid.uuid4().hex
    payload = {
        "req_id": req_id,
        "method": method,
        "path": path,
        "body": body,
        "authorization": authorization,
    }
    event = threading.Event()
    with _pending_lock:
        _pending[req_id] = {"event": event, "result": None}
    try:
        info = mqtt_client.publish(TOPIC_REQ, json.dumps(payload, ensure_ascii=False), qos=1)
        if info.rc != 0:
            return 503, {"error": f"relay publish rc={info.rc}"}
        if not event.wait(timeout=settings.RELAY_TIMEOUT):
            return 504, {"error": "relay timeout: home relay did not respond"}
        with _pending_lock:
            result = _pending.pop(req_id, {}).get("result") or {}
        if result.get("error"):
            return 502, {"error": result["error"]}
        return result.get("status", 502), result.get("body")
    finally:
        with _pending_lock:
            _pending.pop(req_id, None)
