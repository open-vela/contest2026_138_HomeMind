"""WebSocket 推送中枢：把 MQTT 线程收到的事件安全地投到 asyncio 事件循环。"""
import asyncio
import logging

logger = logging.getLogger("ws")

_loop = None
_event_queue = None
_connections = set()


def init(loop):
    global _loop, _event_queue
    _loop = loop
    _event_queue = asyncio.Queue()


def push_event(event: dict):
    """线程安全：MQTT 回调线程调用，经 call_soon_threadsafe 入队。"""
    if _loop and _event_queue:
        _loop.call_soon_threadsafe(_event_queue.put_nowait, event)


async def drain():
    """在 asyncio 循环里消费队列，广播给所有已连接的 WebSocket。"""
    while True:
        ev = await _event_queue.get()
        dead = []
        for ws in list(_connections):
            try:
                await ws.send_json(ev)
            except Exception:
                dead.append(ws)
        for ws in dead:
            _connections.discard(ws)


async def connect(ws):
    await ws.accept()
    _connections.add(ws)


def disconnect(ws):
    _connections.discard(ws)
