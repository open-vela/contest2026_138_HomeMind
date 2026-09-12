from fastapi import APIRouter, Depends, HTTPException
from pydantic import BaseModel
import json
import time
import uuid
import logging
from datetime import datetime
from ..config import settings
from ..db import SessionLocal
from ..models import Command, DeviceBinding
from ..security import get_current_user
from ..mqtt_client import publish_command

logger = logging.getLogger("commands")
router = APIRouter(prefix="/v1", tags=["commands"])


class CmdReq(BaseModel):
    action: str
    params: dict = {}


@router.post("/devices/{device_id}/commands")
def send_command(device_id: str, req: CmdReq,
                 user_id: str = Depends(get_current_user)):
    db = SessionLocal()
    try:
        b = db.query(DeviceBinding).filter_by(
            user_id=user_id, device_id=device_id).first()
        if not b:
            raise HTTPException(status_code=404, detail="device not bound to this user")

        if req.action not in settings.ALLOWED_ACTIONS:
            raise HTTPException(
                status_code=400,
                detail=f"action not allowed: {req.action}. allowed={settings.ALLOWED_ACTIONS}")

        # 演示/审核模式：演示设备对非受信任用户只开放安全动作，
        # 默认禁止 mihome.set_power，防止陌生人控制家庭真实电器。
        if (settings.DEMO_MODE and device_id == settings.DEMO_DEVICE_ID
                and user_id not in settings.TRUSTED_USER_IDS):
            if req.action not in settings.DEMO_ALLOWED_ACTIONS:
                logger.info("demo mode blocked action=%s user=%s", req.action, user_id)
                raise HTTPException(
                    status_code=403,
                    detail=f"demo mode: action not permitted: {req.action}. "
                           f"allowed={settings.DEMO_ALLOWED_ACTIONS}")

        raw_ttl = req.params.get("ttl", settings.COMMAND_TTL_SEC)
        if (isinstance(raw_ttl, bool) or not isinstance(raw_ttl, int)
                or not 1 <= raw_ttl <= max(settings.COMMAND_TTL_SEC, 300)):
            raise HTTPException(status_code=400, detail="ttl must be an integer between 1 and 300 seconds")
        ttl = raw_ttl
        cid = uuid.uuid4().hex
        cmd = Command(
            command_id=cid, user_id=user_id, device_id=device_id,
            action=req.action, params=json.dumps(req.params),
            ttl=ttl, status="queued", created_at=datetime.utcnow())
        db.add(cmd)
        db.commit()

        payload = {
            "command_id": cid,
            "action": req.action,
            "params": req.params,
            "ttl": ttl,
            "ts": int(time.time()),
        }
        # 入队即下发；若 broker 暂不可达，命令保留 queued，由过期扫描处理
        try:
            publish_command(f"device/{device_id}/cmd", payload)
        except Exception as e:
            logger.warning("publish failed (command stays queued): %s", e)

        return {"command_id": cid, "status": "queued"}
    finally:
        db.close()


@router.get("/commands/{command_id}")
def get_command(command_id: str, user_id: str = Depends(get_current_user)):
    db = SessionLocal()
    try:
        c = db.query(Command).filter_by(
            command_id=command_id, user_id=user_id).first()
        if not c:
            raise HTTPException(status_code=404,
                                detail="command not found or not yours")
        return {
            "command_id": c.command_id,
            "device_id": c.device_id,
            "action": c.action,
            "status": c.status,
            "ttl": c.ttl,
            "created_at": c.created_at.isoformat() if c.created_at else None,
            "acked_at": c.acked_at.isoformat() if c.acked_at else None,
            "done_at": c.done_at.isoformat() if c.done_at else None,
        }
    finally:
        db.close()
