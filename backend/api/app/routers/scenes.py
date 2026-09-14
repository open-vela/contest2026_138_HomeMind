"""家庭场景编排：睡前等语义场景 → 白名单动作链 + 待办 + 播报。

设计：
- 场景本地可复现，不依赖云端 LLM 才能执行；
- 动作仅允许 settings.ALLOWED_ACTIONS；
- 待办进入 tasks 表，由 reminder worker 触达；
- 播报走 device/<id>/speak。
"""
import json
import os
import uuid
from datetime import datetime, timedelta

from fastapi import APIRouter, Depends, HTTPException
from pydantic import BaseModel

from .. import xiaoai
from ..config import settings
from ..db import SessionLocal
from ..models import Command, HomeEvent, Intent, Task
from ..mqtt_client import publish_command
from ..security import get_current_user

router = APIRouter(prefix="/v1/scenes", tags=["scenes"])


class SceneReq(BaseModel):
    scene: str = "bedtime"
    device_id: str = ""
    note: str = ""
    dry_run: bool = False


def _now():
    return datetime.utcnow()


def _mk_command(db, user_id, device_id, action, params, ttl=30):
    if action not in settings.ALLOWED_ACTIONS:
        return {"action": action, "status": "rejected",
                "reason": f"action not allowed: {action}"}
    cid = uuid.uuid4().hex
    cmd = Command(
        command_id=cid, user_id=user_id, device_id=device_id,
        action=action, params=json.dumps(params, ensure_ascii=False),
        ttl=ttl, status="queued", created_at=_now())
    db.add(cmd)
    db.commit()
    payload = {
        "command_id": cid, "action": action, "params": params,
        "ttl": ttl, "ts": int(_now().timestamp()),
    }
    try:
        publish_command(f"device/{device_id}/cmd", payload)
        pub = "queued"
    except Exception as e:
        pub = f"publish_failed:{e}"
    return {"action": action, "command_id": cid, "status": pub, "params": params}


def _mk_task(db, user_id, title, note, minutes_from_now, timezone="Asia/Shanghai",
             dedupe_minutes: int = 120):
    """创建待办；同一用户下短时间内已有同标题 pending 任务则复用，不重复建。

    背景：睡前场景会被重复触发（语音、小程序、定时任务），早期版本无条件建单，
    导致"睡前刷牙"累积多条、到期时小爱连续播报同一句话。这里做幂等。
    """
    cutoff = _now() - timedelta(minutes=dedupe_minutes)
    exist = (db.query(Task)
             .filter(Task.user_id == user_id,
                     Task.title == title,
                     Task.status == "pending",
                     Task.created_at >= cutoff)
             .order_by(Task.id.desc())
             .first())
    if exist:
        return {
            "task_id": exist.task_id, "title": title,
            "due_at": exist.due_at.isoformat() if exist.due_at else None,
            "remind_at": exist.remind_at.isoformat() if exist.remind_at else None,
            "status": "pending", "deduped": True,
        }
    due = _now() + timedelta(minutes=minutes_from_now)
    tid = uuid.uuid4().hex
    t = Task(
        task_id=tid, user_id=user_id, title=title, note=note,
        due_at=due, timezone=timezone, remind_at=due,
        status="pending", created_at=_now())
    db.add(t)
    db.commit()
    return {
        "task_id": tid, "title": title, "due_at": due.isoformat(),
        "remind_at": due.isoformat(), "status": "pending", "deduped": False,
    }


def _speak(device_id, text):
    """优先小爱 TTS；失败则回落 MQTT speak（网关→HA notify）。"""
    xa = xiaoai.say(text)
    if xa.get("ok"):
        return {"speak": "xiaoai_tts", "text": text, "detail": xa}
    if not device_id:
        return {"speak": "skipped", "reason": "no device_id", "xiaoai": xa}
    try:
        publish_command(
            f"device/{device_id}/speak",
            {"text": text, "ts": int(_now().timestamp())})
        return {"speak": "mqtt", "text": text, "xiaoai": xa}
    except Exception as e:
        return {"speak": "failed", "error": str(e), "xiaoai": xa}


def _record_intent(db, user_id, scene, device_id, actions,
                   status="handled", reason=""):
    it = Intent(
        intent_id=uuid.uuid4().hex, user_id=user_id,
        text=f"scene:{scene}", intent_type="scene.run",
        action=scene,
        entities=json.dumps({"scene": scene}, ensure_ascii=False),
        params=json.dumps({"actions": actions}, ensure_ascii=False),
        status=status, device_id=device_id, reason=reason,
        created_at=_now(), handled_at=_now())
    db.add(it)
    db.commit()
    return it.intent_id


def _record_event(db, device_id, event_type, payload):
    e = HomeEvent(
        event_id=uuid.uuid4().hex, device_id=device_id or "",
        source="scene", event_type=event_type,
        payload=json.dumps(payload, ensure_ascii=False),
        created_at=_now())
    db.add(e)
    db.commit()
    return e.event_id


def _plan_bedtime(db, user_id, device_id, note, dry_run):
    """睡前场景：板端 LED + 小爱 execute 关灯 + 小爱 TTS + 待办。

    关灯走小爱 execute_text_directive（厂商语音执行链路），
    不再依赖 mihome.set_power 白名单命令；板端 LED 仍走本地 MQTT。
    """
    speak_text = "好的，开始睡前准备。灯我会关上，待办已记下。"
    light_cmd = os.getenv("HA_BEDTIME_VOICE_CMD", "关闭多功能房吸顶灯")
    planned = [("睡前刷牙", 10), ("检查门窗与闹钟", 15)]
    if note:
        planned.append((f"睡前备注：{note[:80]}", 20))
    if dry_run:
        # dry_run 必须无副作用：不建待办、不下发命令，只展示将要做什么
        tasks = [{"title": t, "minutes": m, "status": "dry_run"}
                 for t, m in planned]
    else:
        tasks = [_mk_task(db, user_id, t, "scene:bedtime", m)
                 for t, m in planned]
    executed = []
    if dry_run:
        executed.append({"action": "led.off", "params": {"reason": "bedtime"},
                         "status": "dry_run"})
        executed.append({"action": "xiaoai.execute", "params": {"text": light_cmd},
                         "status": "dry_run"})
        speak = {"speak": "dry_run", "text": speak_text}
    else:
        # 1) 板端 LED
        if "led.off" in settings.ALLOWED_ACTIONS:
            executed.append(_mk_command(
                db, user_id, device_id or "esp32s3-eye",
                "led.off", {"reason": "bedtime"}))
        else:
            executed.append({"action": "led.off", "status": "rejected",
                             "reason": "action not allowed: led.off"})
        # 2) 小爱执行关灯（等价对小爱说一句话）
        xa = xiaoai.execute_directive(light_cmd, silent=True)
        executed.append({
            "action": "xiaoai.execute",
            "params": {"text": light_cmd, "silent": True},
            "status": "ok" if xa.get("ok") else "failed",
            "detail": xa,
        })
        # 3) 小爱 TTS 确认
        speak = _speak(device_id, speak_text)
    return {
        "scene": "bedtime",
        "device_id": device_id,
        "dry_run": dry_run,
        "actions": executed,
        "tasks": tasks,
        "speak": speak,
        "policy": {
            "raw_audio_video_out": "denied",
            "text_only_to_mimo": True,
            "voice_actuator": "xiaoai.execute_text_directive",
            "whitelist": list(settings.ALLOWED_ACTIONS),
        },
    }


SCENE_PLANNERS = {
    "bedtime": _plan_bedtime,
    "sleep": _plan_bedtime,
    "good_night": _plan_bedtime,
}


@router.post("")
def run_scene(req: SceneReq, user_id: str = Depends(get_current_user)):
    scene = (req.scene or "bedtime").strip().lower()
    planner = SCENE_PLANNERS.get(scene)
    if not planner:
        raise HTTPException(status_code=404, detail=f"unknown scene: {scene}")
    db = SessionLocal()
    try:
        plan = planner(db, user_id, req.device_id, req.note, req.dry_run)
        rejected = [a for a in plan["actions"] if a.get("status") == "rejected"]
        if rejected and len(rejected) == len(plan["actions"]):
            status, reason = "rejected", "all actions rejected"
        else:
            status, reason = "handled", ""
        intent_id = _record_intent(
            db, user_id, scene, req.device_id, plan["actions"],
            status=status, reason=reason)
        event_id = _record_event(db, req.device_id, "scene_completed", {
            "scene": scene, "intent_id": intent_id, "dry_run": req.dry_run,
            "actions": plan["actions"], "tasks": plan["tasks"],
        })
        plan["intent_id"] = intent_id
        plan["event_id"] = event_id
        return plan
    finally:
        db.close()


@router.get("/list")
def list_scenes():
    return {"scenes": [
        {"name": "bedtime", "aliases": ["sleep", "good_night"],
         "title": "睡前场景", "desc": "板端LED + 小爱execute关灯 + 小爱TTS + 待办"},
    ]}
