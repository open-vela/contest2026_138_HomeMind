"""家庭资产：资产记录及授权查询/更新（工作包 B / C1.0）。

原则：资产属性（attributes）只按需查询/更新，不整体发送给 MiMo；
owner 留作家庭归属校验（本期单家庭，默认当前用户）。
"""
import json
import uuid
from datetime import datetime
from fastapi import APIRouter, Depends, HTTPException
from pydantic import BaseModel
from ..db import SessionLocal
from ..models import Asset
from ..security import get_current_user

router = APIRouter(prefix="/v1/assets", tags=["assets"])


class AssetReq(BaseModel):
    name: str
    category: str = ""
    location: str = ""
    attributes: dict = {}
    owner: str = ""


def _asset_out(a: Asset) -> dict:
    return {
        "asset_id": a.asset_id,
        "name": a.name,
        "category": a.category,
        "location": a.location,
        "attributes": json.loads(a.attributes or "{}"),
        "owner": a.owner,
        "created_at": a.created_at.isoformat() if a.created_at else None,
        "updated_at": a.updated_at.isoformat() if a.updated_at else None,
    }


@router.post("")
def create_asset(req: AssetReq, user_id: str = Depends(get_current_user)):
    db = SessionLocal()
    try:
        if not req.name.strip():
            raise HTTPException(status_code=400, detail="name required")
        a = Asset(
            asset_id=uuid.uuid4().hex,
            name=req.name.strip(), category=req.category,
            location=req.location,
            attributes=json.dumps(req.attributes, ensure_ascii=False),
            owner=req.owner or user_id)
        db.add(a)
        db.commit()
        db.refresh(a)
        return _asset_out(a)
    finally:
        db.close()


@router.get("")
def list_assets(_user_id: str = Depends(get_current_user),
                category: str = "", location: str = "", limit: int = 50):
    db = SessionLocal()
    try:
        q = db.query(Asset)
        if category:
            q = q.filter_by(category=category)
        if location:
            q = q.filter_by(location=location)
        rows = q.order_by(Asset.id.desc()).limit(min(limit, 200)).all()
        return {"assets": [_asset_out(a) for a in rows]}
    finally:
        db.close()


@router.patch("/{asset_id}")
def update_asset(asset_id: str, req: dict, _user_id: str = Depends(get_current_user)):
    db = SessionLocal()
    try:
        a = db.query(Asset).filter_by(asset_id=asset_id).first()
        if not a:
            raise HTTPException(status_code=404, detail="asset not found")
        if "name" in req and req["name"]:
            a.name = req["name"].strip()
        if "category" in req:
            a.category = req["category"]
        if "location" in req:
            a.location = req["location"]
        if "attributes" in req and isinstance(req["attributes"], dict):
            a.attributes = json.dumps(req["attributes"], ensure_ascii=False)
        a.updated_at = datetime.utcnow()
        db.commit()
        db.refresh(a)
        return _asset_out(a)
    finally:
        db.close()


@router.get("/{asset_id}")
def get_asset(asset_id: str, _user_id: str = Depends(get_current_user)):
    db = SessionLocal()
    try:
        a = db.query(Asset).filter_by(asset_id=asset_id).first()
        if not a:
            raise HTTPException(status_code=404, detail="asset not found")
        return _asset_out(a)
    finally:
        db.close()
