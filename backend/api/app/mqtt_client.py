"""内部 MQTT 客户端（docker 网络内 1883 明文）。

订阅 device/+/ack 与 device/+/status，更新命令状态机与设备状态，
并把事件推给 ws_hub。命令下发由 publish_command() 完成。
"""
import json
import logging
import threading
import time
import paho.mqtt.client as mqtt
from datetime import datetime
from .config import settings
from .db import SessionLocal
from .models import Command, DeviceStatus
from .ws_hub import push_event

logger = logging.getLogger("mqtt")

client = None
_connect_lock = threading.Lock()


def on_disconnect(cli, userdata, rc):
    logger.warning("mqtt disconnected rc=%s, scheduling reconnect", rc)
    if rc != 0:
        threading.Timer(3.0, _reconnect).start()


def _reconnect():
    with _connect_lock:
        try:
            if client is not None:
                client.reconnect()
                logger.info("mqtt reconnected")
        except Exception as e:
            logger.warning("mqtt reconnect failed: %s (retry in 3s)", e)
            threading.Timer(3.0, _reconnect).start()


def _connect_loop():
    """启动时 broker 可能还没起来（DNS 竞态），循环重试直到连上。"""
    while True:
        try:
            with _connect_lock:
                client.connect(settings.MQTT_BROKER_HOST, settings.MQTT_BROKER_PORT, 60)
                client.loop_start()
            logger.info("mqtt connected to %s:%s", settings.MQTT_BROKER_HOST, settings.MQTT_BROKER_PORT)
            return
        except Exception as e:
            logger.warning("mqtt connect failed (%s); retry in 3s", e)
            time.sleep(3)


def _upsert_status(db, device_id, online=True, led=None):
    st = db.query(DeviceStatus).filter_by(device_id=device_id).first()
    if not st:
        st = DeviceStatus(device_id=device_id)
        db.add(st)
    st.online = online
    st.last_seen = datetime.utcnow()
    st.updated_at = datetime.utcnow()
    if led is not None:
        st.led_state = led


def on_connect(cli, userdata, flags, rc):
    logger.info("mqtt connected rc=%s", rc)
    cli.subscribe("device/+/ack")
    cli.subscribe("device/+/status")


def on_message(cli, userdata, msg):
    try:
        topic = msg.topic
        payload = json.loads(msg.payload.decode())
    except Exception as e:
        logger.warning("bad mqtt msg %s: %s", topic, e)
        return
    parts = topic.split("/")
    if len(parts) != 3:
        return
    device_id, kind = parts[1], parts[2]
    db = SessionLocal()
    try:
        if kind == "ack":
            cid = payload.get("command_id")
            st = payload.get("status")
            cmd = db.query(Command).filter_by(command_id=cid).first() if cid else None
            new_status = None
            if cmd:
                if st == "acked" and cmd.status == "queued":
                    cmd.status = "acked"
                    cmd.acked_at = datetime.utcnow()
                elif st == "done" and cmd.status in ("queued", "acked"):
                    cmd.status = "done"
                    cmd.done_at = datetime.utcnow()
                elif st == "expired" and cmd.status in ("queued", "acked"):
                    cmd.status = "expired"
                new_status = cmd.status
                db.commit()
                if new_status:
                    push_event({"type": "command", "device_id": device_id,
                                "command_id": cid, "status": new_status})
            _upsert_status(db, device_id, online=True)
            db.commit()
        elif kind == "status":
            led = payload.get("led")
            online = payload.get("online", True)
            _upsert_status(db, device_id, online=bool(online), led=led)
            db.commit()
            push_event({"type": "status", "device_id": device_id,
                        "online": bool(online), "led": led})
    except Exception as e:
        logger.error("on_message error: %s", e)
    finally:
        db.close()


def start_mqtt():
    global client
    client = mqtt.Client()
    client.on_connect = on_connect
    client.on_message = on_message
    client.on_disconnect = on_disconnect
    threading.Thread(target=_connect_loop, daemon=True).start()


def publish_command(topic: str, payload: dict):
    if client is None:
        raise RuntimeError("mqtt not connected")
    info = client.publish(topic, json.dumps(payload), qos=1)
    if info.rc != 0:
        raise RuntimeError(f"publish rc={info.rc}")
