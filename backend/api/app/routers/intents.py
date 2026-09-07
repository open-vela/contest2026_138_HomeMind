"""家庭意图入口：记录语义理解结果并执行动作白名单校验（工作包 B / C1.0 计划）。

设计要点：
- 意图记录不可变落库（text/intent_type/action/entities/params），供跨端查询与复现；
- 动作白名单与命令白名单一致（settings.ALLOWED_ACTIONS），未知动作标记 rejected 并保留 reason；
- 本接口本期只做"意图记录 + 白名单校验"，不调用 MiMo；语义理解接入在工作包 E。
"""
import json
import uuid
from fastapi import APIRouter, Depends, HTTPException
from pydantic import BaseModel
from ..db import SessionLocal
from ..models import Intent
from ..config import settings
from ..security import get_current_user

router = APIRouter(prefix="/v1/intents", tags=["intents"])


class IntentReq(BaseModel):
    text: str = ""
    intent_type: str = ""
    action: str = ""
    entities: dict = {}
    params: dict = {}
    device_id: str = ""


def _intent_out(i: Intent) -> dict:
    return {
        "intent_id": i.intent_id,
        "text": i.text,
        "intent_type": i.intent_type,
        "action": i.action,
        "entities": json.loads(i.entities or "{}"),
        "params": json.loads(i.params or "{}"),
        "status": i.status,
        "device_id": i.device_id,
        "reason": i.reason,
        "created_at": i.created_at.isoformat() if i.created_at else None,
    }


@router.post("")
def create_intent(req: IntentReq, user_id: str = Depends(get_current_user)):
    db = SessionLocal()
    try:
        action = (req.action or "").strip()
        if action and action not in settings.ALLOWED_ACTIONS:
            it = Intent(
                intent_id=uuid.uuid4().hex, user_id=user_id,
                text=req.text, intent_type=req.intent_type, action=action,
                entities=json.dumps(req.entities, ensure_ascii=False),
                params=json.dumps(req.params, ensure_ascii=False),
                status="rejected", device_id=req.device_id,
                reason=f"action not allowed: {action}")
            db.add(it)
            db.commit()
            db.refresh(it)
            return _intent_out(it)
        it = Intent(
            intent_id=uuid.uuid4().hex, user_id=user_id,
            text=req.text, intent_type=req.intent_type, action=action,
            entities=json.dumps(req.entities, ensure_ascii=False),
            params=json.dumps(req.params, ensure_ascii=False),
            status="pending", device_id=req.device_id)
        db.add(it)
        db.commit()
        db.refresh(it)
        return _intent_out(it)
    finally:
        db.close()


@router.get("")
def list_intents(user_id: str = Depends(get_current_user),
                 limit: int = 50, status: str = ""):
    db = SessionLocal()
    try:
        q = db.query(Intent).filter_by(user_id=user_id)
        if status:
            q = q.filter_by(status=status)
        rows = q.order_by(Intent.id.desc()).limit(min(limit, 200)).all()
        return {"intents": [_intent_out(i) for i in rows]}
    finally:
        db.close()


@router.get("/{intent_id}")
def get_intent(intent_id: str, user_id: str = Depends(get_current_user)):
    db = SessionLocal()
    try:
        i = db.query(Intent).filter_by(
            intent_id=intent_id, user_id=user_id).first()
        if not i:
            raise HTTPException(status_code=404, detail="intent not found")
        return _intent_out(i)
    finally:
        db.close()
