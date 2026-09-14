"""到期提醒 worker：扫描 tasks.remind_at，到点后 MQTT speak + 事件落库。

- 仅处理 status=pending 且 remind_at 非空、已到点且未提醒过的任务；
- 通过 Task.note 中的 marker #reminded 避免重复播报；
- 无 device_id 时写入事件，便于小程序/审计查询。
"""
import json
import logging
import threading
import time
import uuid
from datetime import datetime, timedelta

from .config import settings
from .db import SessionLocal
from .models import HomeEvent, Task
from .mqtt_client import publish_command

logger = logging.getLogger("reminder")
_running = False
_thread = None
MARK = "#reminded"


def _now():
    return datetime.utcnow()


def _publish_speak(device_id: str, text: str) -> bool:
    if not device_id:
        return False
    try:
        publish_command(
            f"device/{device_id}/speak",
            {"text": text, "ts": int(_now().timestamp())})
        return True
    except Exception as e:
        logger.warning("speak publish failed: %s", e)
        return False


def _store_event(device_id: str, event_type: str, payload: dict):
    db = SessionLocal()
    try:
        db.add(HomeEvent(
            event_id=uuid.uuid4().hex,
            device_id=device_id or "",
            source="reminder",
            event_type=event_type,
            payload=json.dumps(payload, ensure_ascii=False),
            created_at=_now()))
        db.commit()
    except Exception as e:
        logger.error("store event failed: %s", e)
    finally:
        db.close()


def process_due_tasks(device_id: str = None) -> int:
    """处理一批到期提醒，返回处理条数。device_id 用于 speak 目标。"""
    now = _now()
    horizon = now + timedelta(seconds=90)
    db = SessionLocal()
    handled = 0
    try:
        rows = (db.query(Task)
                .filter(Task.status == "pending")
                .filter(Task.remind_at != None)  # noqa: E711
                .filter(Task.remind_at <= horizon)
                .order_by(Task.remind_at.asc())
                .limit(50)
                .all())
        for t in rows:
            if MARK in (t.note or ""):
                continue
            title = t.title or "待办"
            text = f"提醒：{title}"
            ok = _publish_speak(device_id, text)
            t.note = ((t.note or "") + f" {MARK}").strip()
            db.add(t)
            db.commit()
            _store_event(device_id or "", "task_reminder", {
                "task_id": t.task_id,
                "title": title,
                "remind_at": t.remind_at.isoformat() if t.remind_at else None,
                "spoken": ok,
            })
            handled += 1
            logger.info("reminded task=%s title=%s spoken=%s",
                        t.task_id, title, ok)
    except Exception as e:
        logger.error("process_due_tasks error: %s", e)
    finally:
        db.close()
    return handled


def _loop(interval_sec: int = 20):
    global _running
    device = getattr(settings, "DEFAULT_SPEAK_DEVICE", "") or "esp32s3-eye"
    logger.info("reminder worker started device=%s interval=%ss",
                device, interval_sec)
    while _running:
        try:
            process_due_tasks(device_id=device)
        except Exception as e:
            logger.error("reminder loop error: %s", e)
        time.sleep(interval_sec)
    logger.info("reminder worker stopped")


def start_reminder_worker(interval_sec: int = 20):
    global _running, _thread
    if _running:
        return
    _running = True
    _thread = threading.Thread(
        target=_loop, args=(interval_sec,), daemon=True, name="reminder")
    _thread.start()


def stop_reminder_worker():
    global _running
    _running = False
