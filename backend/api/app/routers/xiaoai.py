"""小爱音箱能力 API（家庭内，需 JWT）。"""
from fastapi import APIRouter, Depends, HTTPException
from pydantic import BaseModel

from .. import xiaoai
from ..security import get_current_user

router = APIRouter(prefix="/v1/xiaoai", tags=["xiaoai"])


class SayReq(BaseModel):
    text: str


class ExecReq(BaseModel):
    text: str
    silent: bool = True


class VolReq(BaseModel):
    level: float


@router.get("/capabilities")
def caps(_u: str = Depends(get_current_user)):
    return {"capabilities": xiaoai.list_capabilities()}


@router.post("/say")
def say(req: SayReq, _u: str = Depends(get_current_user)):
    if not req.text.strip():
        raise HTTPException(status_code=400, detail="text required")
    return xiaoai.say(req.text.strip())


@router.post("/execute")
def execute(req: ExecReq, _u: str = Depends(get_current_user)):
    if not req.text.strip():
        raise HTTPException(status_code=400, detail="text required")
    return xiaoai.execute_directive(req.text.strip(), silent=req.silent)


@router.post("/button/{name}")
def button(name: str, _u: str = Depends(get_current_user)):
    return xiaoai.press_button(name)


@router.post("/mute")
def mute(req: SayReq, _u: str = Depends(get_current_user)):
    return xiaoai.set_mute(req.text.strip().lower() in ("1", "true", "on", "yes"))


@router.post("/volume")
def volume(req: VolReq, _u: str = Depends(get_current_user)):
    return xiaoai.volume_set(req.level)
