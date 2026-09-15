"""设备级鉴权与限流（端侧上行端点共用）。

设备侧使用共享令牌（Authorization: Bearer MEDIA_TOKEN），与用户 JWT 体系隔离；
令牌未配置时端点整体 503（fail-closed），不通过错误码泄露端点是否存在。
"""
import os
import time

from fastapi import HTTPException, Request

_rate: dict = {}


def device_token() -> str:
    return os.getenv("MEDIA_TOKEN", "")


def check_device_token(request: Request) -> None:
    token = device_token()
    if not token:
        raise HTTPException(status_code=503, detail="device endpoint disabled")
    if request.headers.get("authorization", "") != "Bearer " + token:
        raise HTTPException(status_code=401, detail="invalid device token")


def rate_limit(key: str, per_minute: int = 10) -> None:
    now = time.time()
    window = [t for t in _rate.get(key, []) if now - t < 60]
    if len(window) >= per_minute:
        raise HTTPException(status_code=429, detail="rate limited")
    window.append(now)
    _rate[key] = window
