"""HA 状态桥：观察小爱/米家设备，灯灭可自动进入 HomeMind 睡前场景。

语音入口说明（诚实边界）：
- 小爱音箱硬件唤醒词固定为「小爱同学」，**不能**改成「你好，openvela」。
- 官方 xiaomi_home 集成**读不到**小爱麦克风识别出的文本。
- 可行路径：用户在米家创建场景「我要睡觉了」关灯，或直接说「小爱同学，关灯」；
  HomeMind 检测到多功能房吸顶灯 on→off 后，编排待办+用小爱播报确认。
- 板端「你好，openvela」仅能用板载麦 + kws/家庭 ASR，与小爱麦互不替代。
"""
import json
import logging
import os
import threading
import time
import uuid
from datetime import datetime, timedelta

import httpx

from .config import settings
from .db import SessionLocal
from .models import Command, HomeEvent, Task
from .mqtt_client import publish_command

logger = logging.getLogger("ha_bridge")
_running = False
_thread = None
_last_state = {}
_lock = threading.Lock()
_last_bedtime_ts = 0.0
BEDTIME_COOLDOWN_SEC = 180


def _now():
    return datetime.utcnow()


def _ha_url():
    return os.getenv("HOMEMIND_HA_URL", "http://127.0.0.1:8123").rstrip("/")


def _ha_token():
    return os.getenv("HOMEMIND_HA_TOKEN", "")


def _speak_entity():
    return os.getenv(
        "HOMEMIND_SPEAK_ENTITY",
        "notify.xiaomi_cn_2085562629_lx06_play_text_a_5_1")


def _bedtime_entity():
    return os.getenv(
        "HA_BEDTIME_LIGHT",
        "light.leishi_cn_940744854_eps127_s_2_light")


def _auto_bedtime():
    return os.getenv("HA_AUTO_BEDTIME", "1") not in ("0", "false", "False")


def _entities():
    raw = os.getenv(
        "HA_WATCH_ENTITIES",
        "light.leishi_cn_940744854_eps127_s_2_light,"
        "media_player.xiaomi_cn_2085562629_lx06",
    )
    return [e.strip() for e in raw.split(",") if e.strip()]


def fetch_states():
    token = _ha_token()
    if not token:
        return {}
    ents = set(_entities()) | {_bedtime_entity()}
    out = {}
    try:
        r = httpx.get(
            f"{_ha_url()}/api/states",
            headers={"Authorization": f"Bearer {token}"},
            timeout=5.0,
        )
        r.raise_for_status()
        for e in r.json():
            if e.get("entity_id") in ents:
                out[e["entity_id"]] = {
                    "state": e.get("state"),
                    "friendly_name": e.get("attributes", {}).get("friendly_name"),
                }
    except Exception as e:
        logger.warning("HA fetch failed: %s", e)
    return out


def store_event(device_id, source, event_type, payload):
    db = SessionLocal()
    try:
        eid = uuid.uuid4().hex
        db.add(HomeEvent(
            event_id=eid, device_id=device_id or "home-ha",
            source=source, event_type=event_type,
            payload=json.dumps(payload, ensure_ascii=False),
            created_at=_now()))
        db.commit()
        return eid
    finally:
        db.close()


def _mk_task(db, user_id, title, note, minutes):
    due = _now() + timedelta(minutes=minutes)
    tid = uuid.uuid4().hex
    db.add(Task(
        task_id=tid, user_id=user_id, title=title, note=note,
        due_at=due, timezone="Asia/Shanghai", remind_at=due,
        status="pending", created_at=_now()))
    db.commit()
    return tid


def run_bedtime_from_voice(reason: str, device_id: str = "esp32s3-eye") -> dict:
    """小爱/米家关灯触发的睡前编排：待办 + 板端/小爱播报。"""
    global _last_bedtime_ts
    now = time.time()
    if now - _last_bedtime_ts < BEDTIME_COOLDOWN_SEC:
        return {"skipped": "cooldown"}
    _last_bedtime_ts = now

    db = SessionLocal()
    try:
        tasks = [
            _mk_task(db, "home-system", "睡前刷牙", f"auto:{reason}", 10),
            _mk_task(db, "home-system", "检查门窗与闹钟", f"auto:{reason}", 15),
        ]
        # 板端 LED off（白名单）
        try:
            if "led.off" in settings.ALLOWED_ACTIONS:
                cid = uuid.uuid4().hex
                db.add(Command(
                    command_id=cid, user_id="home-system", device_id=device_id,
                    action="led.off", params=json.dumps({"reason": reason}),
                    ttl=30, status="queued", created_at=_now()))
                db.commit()
                publish_command(f"device/{device_id}/cmd", {
                    "command_id": cid, "action": "led.off",
                    "params": {"reason": reason}, "ttl": 30,
                    "ts": int(_now().timestamp()),
                })
        except Exception as e:
            logger.warning("led.off publish: %s", e)

        text = "好的，进入睡前模式。待办已记下，晚安。"
        # 小爱播报
        try:
            ent = _speak_entity()
            tok = _ha_token()
            httpx.post(
                f"{_ha_url()}/api/services/notify/send_message",
                headers={"Authorization": f"Bearer {tok}"},
                json={"entity_id": ent, "message": text},
                timeout=8.0)
            spoken = True
        except Exception as e:
            logger.warning("speak failed: %s", e)
            spoken = False

        store_event(device_id, "ha", "bedtime_scene_auto", {
            "reason": reason, "tasks": tasks, "spoken": spoken, "text": text,
            "voice_front": "xiaoai_mic_via_mihome_light",
        })
        return {"tasks": tasks, "spoken": spoken, "text": text}
    finally:
        db.close()


def on_state_change(entity_id, old, new, meta):
    kind = "light" if entity_id.startswith("light.") else (
        "media_player" if entity_id.startswith("media_player.") else "ha")
    et = "device_state_changed"
    if entity_id.startswith("light.") and old != new:
        et = "light_state_changed"
    eid = store_event(
        "", "ha", et,
        {
            "entity_id": entity_id,
            "old": old,
            "new": new,
            "friendly_name": meta.get("friendly_name"),
            "via": "xiaomi_home_ha",
            "voice_front": ("xiaoai" if entity_id.startswith("media_player.xiaomi_cn")
                            or "lx0" in entity_id else "mihome_via_ha"),
        })
    logger.info("HA change %s %s->%s event=%s", entity_id, old, new, eid)

    if (entity_id == _bedtime_entity() and old == "on" and new == "off"
            and _auto_bedtime()):
        try:
            result = run_bedtime_from_voice("light_off")
            logger.info("auto bedtime from light_off: %s", result)
        except Exception as e:
            logger.error("auto bedtime failed: %s", e)
    return eid


def poll_once():
    states = fetch_states()
    with _lock:
        for eid, meta in states.items():
            st = meta.get("state")
            old = _last_state.get(eid)
            if old is None:
                _last_state[eid] = st
                continue
            if old != st:
                _last_state[eid] = st
                try:
                    on_state_change(eid, old, st, meta)
                except Exception as e:
                    logger.error("on_state_change error: %s", e)
    return len(states)


def _loop(interval):
    global _running
    logger.info(
        "HA bridge started url=%s entities=%s auto_bedtime=%s interval=%ss",
        _ha_url(), _entities(), _auto_bedtime(), interval)
    while _running:
        try:
            poll_once()
        except Exception as e:
            logger.error("ha bridge loop: %s", e)
        time.sleep(interval)
    logger.info("HA bridge stopped")


def start_ha_bridge(interval_sec: int = 5):
    global _running, _thread
    if _running:
        return
    if not _ha_token():
        logger.warning("HOMEMIND_HA_TOKEN missing; HA bridge not started")
        return
    _running = True
    _thread = threading.Thread(
        target=_loop, args=(interval_sec,), daemon=True, name="ha-bridge")
    _thread.start()


def stop_ha_bridge():
    global _running
    _running = False
