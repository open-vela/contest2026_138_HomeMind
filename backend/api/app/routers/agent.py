"""端云协同语义 API：端侧上报文本 → 私有云规划 → 可选执行。

POST /v1/agent/plan     {"text":"我准备睡觉了","execute":true,"device_id":"esp32s3-eye"}
GET  /v1/agent/plan/{plan_id}  查历史（从 intents）
"""
import json

from fastapi import APIRouter, Depends, HTTPException
from pydantic import BaseModel

from ..agent_plan import execute_plan, persist_plan, plan_text
from ..db import SessionLocal
from ..models import Intent
from ..security import get_current_user

router = APIRouter(prefix="/v1/agent", tags=["agent"])


class PlanReq(BaseModel):
    text: str
    execute: bool = True
    device_id: str = "esp32s3-eye"
    use_llm: bool = True


@router.post("/plan")
def create_plan(req: PlanReq, user_id: str = Depends(get_current_user)):
    if not req.text.strip():
        raise HTTPException(status_code=400, detail="text required")
    # 原始音频不出站；仅处理文本
    plan = plan_text(req.text.strip(), use_llm=req.use_llm)
    persist_plan(user_id, plan, req.device_id)
    if not req.execute or not plan.get("actions"):
        return {"plan": plan, "executed": False}
    result = execute_plan(plan, device_id=req.device_id or "esp32s3-eye")
    return {"plan": plan, "executed": True, "result": result}


@router.get("/plan/{plan_id}")
def get_plan(plan_id: str, _u: str = Depends(get_current_user)):
    db = SessionLocal()
    try:
        it = db.query(Intent).filter_by(intent_id=plan_id).first()
        if not it:
            raise HTTPException(status_code=404, detail="plan not found")
        return {
            "plan_id": it.intent_id,
            "text": it.text,
            "intent_type": it.intent_type,
            "status": it.status,
            "reason": it.reason,
            "entities": json.loads(it.entities or "{}"),
            "created_at": it.created_at.isoformat() if it.created_at else None,
        }
    finally:
        db.close()


@router.get("/health")
def agent_health(_u: str = Depends(get_current_user)):
    """语义后端能力自检：私有本地模型 / 外部模型 / 规则回落。"""
    import os
    from .. import local_asr, local_llm
    st = local_llm.status()
    return {
        # 家庭局域网内的本地大模型（默认首选，数据不出家庭）
        "local_llm": st,
        # 家庭局域网内的本地语音识别（唤醒后语音闭环，音频不出家庭）
        "local_asr": local_asr.status(),
        # 外部大模型：仅在显式允许出站时启用
        "external_llm_configured": bool(os.getenv("MIMO_API_KEY")),
        "external_llm_allowed": os.getenv(
            "PRIVACY_ALLOW_EXTERNAL_LLM", "0") not in ("0", "false", "False", ""),
        "fallback": "local_rules",
        "privacy_mode": os.getenv("PRIVACY_MODE", "1") not in (
            "0", "false", "False", ""),
        "note": "raw audio/video never leaves LAN by default",
    }
