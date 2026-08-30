from fastapi import APIRouter, WebSocket, WebSocketDisconnect, Query
from ..db import SessionLocal
from ..models import DeviceBinding, DeviceStatus
from ..security import decode_token
from ..ws_hub import connect as ws_connect, disconnect as ws_disconnect

router = APIRouter(tags=["ws"])


@router.websocket("/v1/ws/app")
async def ws_app(websocket: WebSocket, token: str = Query(None)):
    """鉴权后推送状态变化与命令 ACK；断线重连后由客户端补拉快照。"""
    payload = decode_token(token, "access") if token else None
    if not payload:
        await websocket.close(code=4401)
        return
    user_id = payload["sub"]
    await ws_connect(websocket)
    try:
        db = SessionLocal()
        try:
            binds = db.query(DeviceBinding).filter_by(user_id=user_id).all()
            snap = []
            for b in binds:
                st = db.query(DeviceStatus).filter_by(device_id=b.device_id).first()
                snap.append({
                    "device_id": b.device_id,
                    "name": b.name,
                    "online": bool(st.online) if st else False,
                    "led_state": st.led_state if st else "unknown",
                })
        finally:
            db.close()
        await websocket.send_json({"type": "snapshot", "devices": snap})

        # 心跳保活：小程序侧会发 ping，这里仅接收避免断开
        while True:
            await websocket.receive_text()
    except WebSocketDisconnect:
        ws_disconnect(websocket)
    except Exception:
        ws_disconnect(websocket)
