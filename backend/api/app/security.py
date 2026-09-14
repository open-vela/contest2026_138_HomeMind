"""鉴权：JWT 签发/校验 + 微信 code2Session。"""
import time
import uuid
import httpx
import jwt
from fastapi import Depends, HTTPException, status
from fastapi.security import HTTPBearer, HTTPAuthorizationCredentials
from .config import settings
from .db import SessionLocal
from .models import User

bearer = HTTPBearer(auto_error=False)

WX_CODE2SESSION = "https://api.weixin.qq.com/sns/jscode2session"


def create_tokens(user_id: str):
    now = int(time.time())
    access = jwt.encode(
        {"sub": user_id, "typ": "access", "iat": now,
         "exp": now + settings.ACCESS_TOKEN_TTL_MIN * 60},
        settings.JWT_SECRET, algorithm=settings.JWT_ALG,
    )
    refresh = jwt.encode(
        {"sub": user_id, "typ": "refresh", "iat": now,
         "exp": now + settings.REFRESH_TOKEN_TTL_DAYS * 86400},
        settings.JWT_SECRET, algorithm=settings.JWT_ALG,
    )
    return access, refresh


def decode_token(token: str, typ: str):
    try:
        payload = jwt.decode(token, settings.JWT_SECRET, algorithms=[settings.JWT_ALG])
    except jwt.PyJWTError:
        return None
    if payload.get("typ") != typ:
        return None
    return payload


def get_current_user(creds: HTTPAuthorizationCredentials = Depends(bearer)) -> str:
    """返回 user_id，任何失败都 401。"""
    if not creds:
        raise HTTPException(status_code=401, detail="missing token")
    payload = decode_token(creds.credentials, "access")
    if not payload:
        raise HTTPException(status_code=401, detail="invalid or expired token")
    return payload["sub"]


def wechat_code2session(code: str) -> dict:
    """调微信 jscode2session，返回 {openid, session_key} 或 {errcode, errmsg}。"""
    resp = httpx.get(
        WX_CODE2SESSION,
        params={
            "appid": settings.WX_APPID,
            "secret": settings.WX_APPSECRET,
            "js_code": code,
            "grant_type": "authorization_code",
        },
        timeout=10,
    )
    resp.raise_for_status()
    return resp.json()


def get_or_create_user(openid: str) -> User:
    db = SessionLocal()
    try:
        u = db.query(User).filter_by(openid=openid).first()
        if not u:
            u = User(user_id=str(uuid.uuid4()), openid=openid)
            db.add(u)
            db.commit()
            db.refresh(u)
        return u
    finally:
        db.close()
