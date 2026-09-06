"""设备媒体端点：RGB565 帧 / PCM 录音 上行，云端转码后调 MiMo 全模态。

- POST /v1/media/frame?w=320&h=240&swap=1&q=提示词  body=RGB565 裸帧
  -> 云端转 JPEG -> MiMo 图像理解 -> {"text": 描述}
- POST /v1/media/audio?rate=16000  body=16bit 单声道 PCM
  -> 云端包 WAV 头 -> MiMo 音频理解转写 -> {"text": 转写文本}
- POST /v1/media/announce  {"device_id":..., "text":...}
  -> MQTT device/<id>/speak -> 家庭网关 -> HA notify -> 小爱音箱播报

鉴权：设备级共享令牌（Authorization: Bearer MEDIA_TOKEN），与用户 JWT 体系隔离；
MEDIA_TOKEN / MIMO_API_KEY 未配置时端点整体 503（fail-closed），不泄露存在性。
"""
import base64
import io
import json
import logging
import os
import struct
import time

import httpx
from fastapi import APIRouter, HTTPException, Request

from ..mqtt_client import publish_command

logger = logging.getLogger("media")
router = APIRouter(prefix="/v1/media", tags=["media"])

MAX_FRAME_BYTES = 400 * 1024   # 320x240 RGB565 = 150000，留足余量
MAX_AUDIO_BYTES = 320 * 1024   # 16k/16bit/mono 约 10 秒
MAX_TEXT = 300


def _media_token() -> str:
    return os.getenv("MEDIA_TOKEN", "")


def _mimo_key() -> str:
    return os.getenv("MIMO_API_KEY", "")


def _check_device_token(request: Request) -> None:
    token = _media_token()
    if not token:
        raise HTTPException(status_code=503, detail="media endpoint disabled")
    auth = request.headers.get("authorization", "")
    if auth != "Bearer " + token:
        raise HTTPException(status_code=401, detail="invalid device token")


_rate: dict = {}


def _rate_limit(key: str, per_minute: int = 10) -> None:
    now = time.time()
    window = [t for t in _rate.get(key, []) if now - t < 60]
    if len(window) >= per_minute:
        raise HTTPException(status_code=429, detail="rate limited")
    window.append(now)
    _rate[key] = window


def _mimo_chat(content: list, timeout: float = 60.0) -> str:
    key = _mimo_key()
    if not key:
        raise HTTPException(status_code=503, detail="MIMO_API_KEY not configured")
    model = os.getenv("MIMO_MODEL", "mimo-v2.5")
    body = {"model": model, "messages": [{"role": "user", "content": content}]}
    try:
        resp = httpx.post(
            os.getenv("MIMO_API_BASE", "https://api.xiaomimimo.com")
            + "/v1/chat/completions",
            json=body,
            headers={"Authorization": "Bearer " + key},
            timeout=timeout,
        )
    except httpx.HTTPError as e:
        logger.warning("mimo upstream error: %s", e)
        raise HTTPException(status_code=502, detail="mimo upstream error")
    if resp.status_code != 200:
        logger.warning("mimo status=%s body=%.200s", resp.status_code, resp.text)
        raise HTTPException(status_code=502, detail="mimo rejected request")
    try:
        return resp.json()["choices"][0]["message"]["content"] or ""
    except (KeyError, IndexError, ValueError):
        raise HTTPException(status_code=502, detail="mimo unexpected response")


def _rgb565_to_jpeg(raw: bytes, width: int, height: int, swap: bool) -> bytes:
    """RGB565（可选字节序交换）-> JPEG。优先 numpy 向量化，缺失时退化纯 Python。"""
    try:
        import numpy as np
        from PIL import Image

        arr = np.frombuffer(raw[: width * height * 2], dtype="<u2" if swap else ">u2")
        r = ((arr >> 11) & 0x1F) << 3
        g = ((arr >> 5) & 0x3F) << 2
        b = (arr & 0x1F) << 3
        rgb = np.concatenate([r, g, b]).reshape(3, height, width).transpose(1, 2, 0)
        img = Image.fromarray(rgb.astype("uint8"), "RGB")
        buf = io.BytesIO()
        img.save(buf, format="JPEG", quality=85)
        return buf.getvalue()
    except ImportError:
        # 无 numpy/Pillow 依赖时的兜底：逐像素转换 + Pillow 缺失则 503
        from PIL import Image
        img = Image.new("RGB", (width, height))
        pix = img.load()
        fmt = "<H" if swap else ">H"
        for i in range(width * height):
            v = struct.unpack_from(fmt, raw, i * 2)[0]
            pix[i % width, i // width] = (
                ((v >> 11) & 0x1F) << 3, ((v >> 5) & 0x3F) << 2, (v & 0x1F) << 3)
        buf = io.BytesIO()
        img.save(buf, format="JPEG", quality=85)
        return buf.getvalue()


@router.post("/frame")
async def media_frame(request: Request, w: int = 320, h: int = 240,
                      swap: int = 1, q: str = "简要描述这张图片里的内容"):
    _check_device_token(request)
    _rate_limit("frame")
    raw = await request.body()
    if not (w and h and 0 < w * h * 2 <= MAX_FRAME_BYTES):
        raise HTTPException(status_code=400, detail="bad frame size")
    if len(raw) < w * h * 2:
        raise HTTPException(status_code=400, detail="frame too short")
    jpeg = _rgb565_to_jpeg(raw[: w * h * 2], w, h, bool(swap))
    b64 = base64.b64encode(jpeg).decode()
    answer = _mimo_chat([
        {"type": "image_url",
         "image_url": {"url": "data:image/jpeg;base64," + b64}},
        {"type": "text", "text": (q or "简要描述这张图片")[:200]},
    ])
    return {"text": answer.strip()[:MAX_TEXT], "jpeg_bytes": len(jpeg)}


def _pcm_to_wav(pcm: bytes, rate: int) -> bytes:
    data = pcm
    byte_rate = rate * 2
    header = b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVE"
    header += b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, rate, byte_rate, 2, 16)
    header += b"data" + struct.pack("<I", len(data))
    return header + data


@router.post("/audio")
async def media_audio(request: Request, rate: int = 16000, q: str = ""):
    _check_device_token(request)
    _rate_limit("audio")
    raw = await request.body()
    if not (8000 <= rate <= 48000 and 0 < len(raw) <= MAX_AUDIO_BYTES):
        raise HTTPException(status_code=400, detail="bad audio size/rate")
    wav_b64 = base64.b64encode(_pcm_to_wav(raw, rate)).decode()
    prompt = (q or "请逐字转写这段音频的中文内容，只输出转写文本，不要任何解释")[:200]
    answer = _mimo_chat([
        {"type": "input_audio",
         "input_audio": {"data": wav_b64, "format": "wav"}},
        {"type": "text", "text": prompt},
    ], timeout=90.0)
    return {"text": answer.strip()[:MAX_TEXT]}


@router.post("/announce")
async def media_announce(request: Request):
    _check_device_token(request)
    _rate_limit("announce", per_minute=6)
    try:
        payload = json.loads(await request.body())
    except ValueError:
        raise HTTPException(status_code=400, detail="bad json")
    device_id = payload.get("device_id")
    text = payload.get("text")
    if not isinstance(device_id, str) or not device_id.replace("-", ""):
        raise HTTPException(status_code=400, detail="bad device_id")
    if not isinstance(text, str) or not text.strip():
        raise HTTPException(status_code=400, detail="text required")
    text = text.strip()[:MAX_TEXT]
    try:
        publish_command(f"device/{device_id}/speak",
                        {"text": text, "ts": int(time.time())})
    except Exception as e:
        logger.warning("announce publish failed: %s", e)
        raise HTTPException(status_code=503, detail="broker unavailable")
    logger.info("announce queued device=%s len=%d", device_id, len(text))
    return {"queued": True}
