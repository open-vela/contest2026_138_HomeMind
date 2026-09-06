from fastapi import APIRouter, Depends, HTTPException
from pydantic import BaseModel
import json
from ..db import SessionLocal
from ..models import DeviceBinding, DeviceStatus
from ..security import get_current_user

router = APIRouter(prefix="/v1", tags=["devices"])


class BindReq(BaseModel):
    device_id: str
    name: str = ""


def _parse_mihome_entities(raw) -> list:
    if not raw:
        return []
    try:
        data = json.loads(raw)
        return data if isinstance(data, list) else []
    except (ValueError, TypeError):
        return []


@router.get("/devices")
def list_devices(user_id: str = Depends(get_current_user)):
    db = SessionLocal()
    try:
        binds = db.query(DeviceBinding).filter_by(user_id=user_id).all()
        out = []
        for b in binds:
            st = db.query(DeviceStatus).filter_by(device_id=b.device_id).first()
            out.append({
                "device_id": b.device_id,
                "name": b.name,
                "online": bool(st.online) if st else False,
                "led_state": st.led_state if st else "unknown",
                "mihome_entities": _parse_mihome_entities(
                    st.mihome_entities if st else ""),
                "last_seen": st.last_seen.isoformat() if st and st.last_seen else None,
            })
        return {"devices": out}
    finally:
        db.close()


@router.post("/devices")
def bind_device(req: BindReq, user_id: str = Depends(get_current_user)):
    db = SessionLocal()
    try:
        existing = db.query(DeviceBinding).filter_by(
            user_id=user_id, device_id=req.device_id).first()
        if existing:
            return {"device_id": existing.device_id, "name": existing.name}
        b = DeviceBinding(user_id=user_id, device_id=req.device_id,
                          name=req.name or req.device_id)
        db.add(b)
        db.commit()
        return {"device_id": b.device_id, "name": b.name}
    finally:
        db.close()


@router.delete("/devices/{device_id}")
def unbind_device(device_id: str, user_id: str = Depends(get_current_user)):
    """Remove only the user's binding; retain device status and command audit data."""
    db = SessionLocal()
    try:
        binding = db.query(DeviceBinding).filter_by(
            user_id=user_id, device_id=device_id).first()
        if not binding:
            raise HTTPException(status_code=404, detail="device not bound to this user")
        db.delete(binding)
        db.commit()
        return {"device_id": device_id, "status": "unbound"}
    finally:
        db.close()
