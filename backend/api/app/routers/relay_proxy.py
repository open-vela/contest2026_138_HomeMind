"""转发路由：RELAY_MODE=true 时，业务接口经家庭出站通道执行。

覆盖：/v1/intents、/v1/events、/v1/tasks、/v1/assets（B3 家庭业务）。
命令下发 /v1/devices/.../commands 本期仍走云端现链路（网关双 MQTT），
入口转发二期再切换，避免双链路状态机冲突。
"""
import logging
from fastapi import APIRouter, Request
from fastapi.responses import JSONResponse

from ..config import settings
from ..relay_client import forward

logger = logging.getLogger("relay")

router = APIRouter(tags=["relay"])

# (method, path) -> 是否转发（POST/GET/PATCH 全转发，支持 path 参数）
_FORWARD_PATHS = [
    "/v1/intents",
    "/v1/intents/{intent_id}",
    "/v1/events",
    "/v1/events/{event_id}",
    "/v1/tasks",
    "/v1/tasks/{task_id}",
    "/v1/assets",
    "/v1/assets/{asset_id}",
]


async def _do_forward(request: Request, path: str):
    auth = request.headers.get("Authorization", "")
    body = None
    if request.method in ("POST", "PATCH", "PUT"):
        try:
            body = await request.json()
        except Exception:
            body = None
    query = request.url.query
    full = path + (f"?{query}" if query else "")
    status, resp_body = forward(request.method, full, body, auth)
    if resp_body is None:
        resp_body = {}
    if not isinstance(resp_body, (dict, list)):
        resp_body = {"detail": str(resp_body)}
    return JSONResponse(content=resp_body, status_code=int(status))


@router.api_route("/v1/intents", methods=["GET", "POST"])
async def relay_intents(request: Request):
    return await _do_forward(request, "/v1/intents")


@router.api_route("/v1/intents/{intent_id}", methods=["GET"])
async def relay_intent_get(request: Request, intent_id: str):
    return await _do_forward(request, f"/v1/intents/{intent_id}")


@router.api_route("/v1/events", methods=["GET", "POST"])
async def relay_events(request: Request):
    return await _do_forward(request, "/v1/events")


@router.api_route("/v1/events/{event_id}", methods=["GET"])
async def relay_event_get(request: Request, event_id: str):
    return await _do_forward(request, f"/v1/events/{event_id}")


@router.api_route("/v1/tasks", methods=["GET", "POST"])
async def relay_tasks(request: Request):
    return await _do_forward(request, "/v1/tasks")


@router.api_route("/v1/tasks/{task_id}", methods=["GET", "PATCH"])
async def relay_task(request: Request, task_id: str):
    return await _do_forward(request, f"/v1/tasks/{task_id}")


@router.api_route("/v1/assets", methods=["GET", "POST"])
async def relay_assets(request: Request):
    return await _do_forward(request, "/v1/assets")


@router.api_route("/v1/assets/{asset_id}", methods=["GET", "PATCH"])
async def relay_asset(request: Request, asset_id: str):
    return await _do_forward(request, f"/v1/assets/{asset_id}")
