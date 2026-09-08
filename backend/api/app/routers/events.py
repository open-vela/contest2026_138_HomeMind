"""家庭感知事件：保存真实事件、时间、来源设备与版本，提供家庭内查询（工作包 B / C1.0）。

来源约定：vision / voice / sensor / mqtt / manual。
事件类型示例：person_detected / motion / wake_word / tts_announced / command_result。
payload 为 JSON 载荷；不可变追加，仅查询。
"""
import json
import uuid
from fastapi import APIRouter, Depends, HTTPException
from pydantic import BaseModel
from ..db import SessionLocal
from ..models import HomeEvent
from ..security import get_current_user

router = APIRouter(prefix="/v1/events", tags=["events"])


class EventReq(BaseModel):
    device_id: str = ""
    source: str = "manual"
    event_type: str = "info"
    payload: dict = {}


def _event_out(e: HomeEvent) -> dict:
    return {
        "event_id": e.event_id,
        "device_id": e.device_id,
        "source": e.source,
        "event_type": e.event_type,
        "payload": json.loads(e.payload or "{}"),
        "created_at": e.created_at.isoformat() if e.created_at else None,
    }


@router.post("")
def create_event(req: EventReq, _user_id: str = Depends(get_current_user)):
    db = SessionLocal()
    try:
        e = HomeEvent(
            event_id=uuid.uuid4().hex,
            device_id=req.device_id,
            source=req.source,
            event_type=req.event_type,
            payload=json.dumps(req.payload, ensure_ascii=False))
        db.add(e)
        db.commit()
        db.refresh(e)
        return _event_out(e)
    finally:
        db.close()


@router.get("")
def list_events(_user_id: str = Depends(get_current_user),
                device_id: str = "", event_type: str = "",
                source: str = "", limit: int = 50, before: str = ""):
    db = SessionLocal()
    try:
        q = db.query(HomeEvent)
        if device_id:
            q = q.filter_by(device_id=device_id)
        if event_type:
            q = q.filter_by(event_type=event_type)
        if source:
            q = q.filter_by(source=source)
        rows = q.order_by(HomeEvent.id.desc()).limit(min(limit, 200)).all()
        return {"events": [_event_out(e) for e in rows]}
    finally:
        db.close()


@router.get("/{event_id}")
def get_event(event_id: str, _user_id: str = Depends(get_current_user)):
    db = SessionLocal()
    try:
        e = db.query(HomeEvent).filter_by(event_id=event_id).first()
        if not e:
            raise HTTPException(status_code=404, detail="event not found")
        return _event_out(e)
    finally:
        db.close()
