"""感知事件入库：接收板端 MQTT 感知消息，落 HomeEvent，可选触发待办。

板端 topic：
- device/<id>/event  JSON: {type: person_detected|kws_wake|motion, ...}
- homemind           兼容旧 vision local 发布

策略：
- person_detected + detected=1：落事件；连续出现时创建“有人进入”待办（冷却 10 分钟）。
- kws_wake：落 wake_word 事件，并可创建“语音唤醒”审计事件。
- 原始音视频 payload 不入库、不出站。
"""
import json
import logging
import time
import uuid
from datetime import datetime, timedelta

from .db import SessionLocal
from .models import HomeEvent, Task

logger = logging.getLogger("perception")

_cooldown: dict = {}
PERSON_COOLDOWN_SEC = 600


def _now():
    return datetime.utcnow()


def ingest_event(device_id: str, payload: dict) -> dict:
    et = (payload or {}).get("type") or (payload or {}).get("event_type") or "info"
    source = "vision" if et in ("person_detected", "motion") else (
        "voice" if et in ("kws_wake", "wake_word") else "mqtt")
    db = SessionLocal()
    try:
        eid = uuid.uuid4().hex
        db.add(HomeEvent(
            event_id=eid, device_id=device_id or "",
            source=source, event_type=et,
            payload=json.dumps(payload, ensure_ascii=False),
            created_at=_now()))
        db.commit()
        extra = {"event_id": eid, "event_type": et}
        # 感知触发任务：人员出现且超过冷却
        if et == "person_detected" and payload.get("detected"):
            key = f"person:{device_id}"
            last = _cooldown.get(key, 0)
            now = time.time()
            if now - last > PERSON_COOLDOWN_SEC:
                _cooldown[key] = now
                task_id = _maybe_create_task(
                    db, title="检测到有人出现",
                    note=f"vision person_detected score={payload.get('score')}",
                    minutes=60)
                if task_id:
                    extra["triggered_task_id"] = task_id
        if et in ("kws_wake", "wake_word"):
            _cooldown[f"kws:{device_id}"] = time.time()
        return extra
    except Exception as e:
        logger.error("ingest_event failed: %s", e)
        return {"error": str(e)}
    finally:
        db.close()


def _maybe_create_task(db, title: str, note: str, minutes: int = 60):
    """为事件创建待办；user_id 使用事件侧虚拟家庭用户 home。"""
    user_id = "home-system"
    due = _now() + timedelta(minutes=minutes)
    tid = uuid.uuid4().hex
    try:
        db.add(Task(
            task_id=tid, user_id=user_id, title=title, note=note,
            due_at=due, timezone="Asia/Shanghai", remind_at=due,
            status="pending", created_at=_now()))
        db.commit()
        return tid
    except Exception as e:
        logger.warning("create task failed: %s", e)
        return None
