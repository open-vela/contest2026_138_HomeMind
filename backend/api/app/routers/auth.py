from fastapi import APIRouter, HTTPException
from pydantic import BaseModel
from ..config import settings
from ..db import SessionLocal
from ..models import DeviceBinding, User
from ..security import wechat_code2session, get_or_create_user, create_tokens, decode_token

router = APIRouter(prefix="/v1/auth", tags=["auth"])


class LoginReq(BaseModel):
    code: str


class RefreshReq(BaseModel):
    refresh_token: str


@router.post("/wechat/login")
def wechat_login(req: LoginReq):
    """小程序 wx.login 拿到的 code 换 openid，签发 access/refresh token。"""
    if not req.code:
        raise HTTPException(status_code=400, detail="code required")
    try:
        resp = wechat_code2session(req.code)
    except Exception as e:
        raise HTTPException(status_code=502, detail=f"wechat upstream error: {e}")
    if "errcode" in resp and resp["errcode"] not in (0, None):
        raise HTTPException(status_code=401, detail=f"wechat auth failed: {resp.get('errmsg')}")
    openid = resp.get("openid")
    if not openid:
        raise HTTPException(status_code=401, detail="no openid in wechat response")

    user = get_or_create_user(openid)

    # 首次登录自动绑定演示设备，使开箱即可下发命令（验证版便利）
    db = SessionLocal()
    try:
        b = db.query(DeviceBinding).filter_by(user_id=user.user_id).first()
        if not b:
            b = DeviceBinding(user_id=user.user_id,
                              device_id=settings.DEMO_DEVICE_ID,
                              name="HomeMind 设备")
            db.add(b)
            db.commit()
    finally:
        db.close()

    access, refresh = create_tokens(user.user_id)
    return {
        "access_token": access,
        "refresh_token": refresh,
        "token_type": "Bearer",
        "expires_in": settings.ACCESS_TOKEN_TTL_MIN * 60,
        "user_id": user.user_id,
    }


@router.post("/refresh")
def refresh(req: RefreshReq):
    payload = decode_token(req.refresh_token, "refresh")
    if not payload:
        raise HTTPException(status_code=401, detail="invalid refresh token")
    access, refresh = create_tokens(payload["sub"])
    return {
        "access_token": access,
        "refresh_token": refresh,
        "token_type": "Bearer",
        "expires_in": settings.ACCESS_TOKEN_TTL_MIN * 60,
    }
