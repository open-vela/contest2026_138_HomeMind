"""家庭私有云的本地对话端点：端侧把文本发来，本机完成语义规划与执行。

为什么需要它（对应方案第 2 点"端云协同"）：
  固件 voice 流程是「录音 -> /v1/media/audio 转写 -> POST llm_path 取回答」。
  把端侧 llm_path 指向本端点，端侧不必改一行代码、不必重刷固件，就能把
  "问公网大模型"切换成"家庭私有云规划并执行"，且全程不出局域网。

请求（兼容 OpenAI Chat Completions 形状，便于端侧复用已有 JSON 序列化）：
  {"model":"homemind-local","messages":[{"role":"user","content":"把灯打开（你是…）"}]}
  也接受 {"text":"把灯打开"} 与 ?text=... 形式。
响应：
  {"choices":[{"message":{"role":"assistant","content":"好的，开灯。"}}],
   "plan":{...},"executed":true,"source":"local_llm"}

鉴权：与端侧其它上行端点一致，用设备级共享令牌（MEDIA_TOKEN）。
播报：本端点不主动让小爱播报（announce=False），由端侧拿到 content 后自行播报，
      避免同一条回复被播两次。
"""
import json
import logging
import re

from fastapi import APIRouter, HTTPException, Request

from ..agent_plan import execute_plan, persist_plan, plan_text
from ..device_auth import check_device_token, rate_limit

logger = logging.getLogger("llm_local")
router = APIRouter(prefix="/v1/llm", tags=["llm"])

# 端侧固件在用户文本后面拼接的提示词标记，转发前要剥掉
_INSTRUCTION_MARKS = ("（你是家庭机器人", "(你是家庭机器人")
_MAX_INPUT = 500

_MODEL_REPLY = re.compile(r"^(?:用户|user)\s*[:：]\s*", re.I)


def _clean(text: str) -> str:
    """剥掉端侧拼接的提示词，只留用户真正说的话。"""
    t = (text or "").strip()
    for mark in _INSTRUCTION_MARKS:
        i = t.find(mark)
        if i > 0:
            t = t[:i].strip()
    t = _MODEL_REPLY.sub("", t)
    return t.strip()[:_MAX_INPUT]


def _spoken(text: str, limit: int = 60) -> str:
    """清洗成端侧可以直接播报的一句话（端侧按 "content":" 截取到下一个引号）。"""
    t = (text or "").replace('"', "").replace("\\", "").replace("\n", " ")
    t = t.strip()
    return t[:limit]


def _extract(body: dict) -> str:
    if isinstance(body.get("text"), str) and body["text"].strip():
        return body["text"]
    msgs = body.get("messages")
    if isinstance(msgs, list):
        for m in reversed(msgs):
            if isinstance(m, dict) and m.get("role") != "system":
                c = m.get("content")
                if isinstance(c, str):
                    return c
                if isinstance(c, list):   # 兼容多模态 content 数组
                    for part in c:
                        if isinstance(part, dict) and part.get("type") == "text":
                            return part.get("text") or ""
    return ""


@router.post("/chat")
async def llm_chat(request: Request, text: str = "", device_id: str = "esp32s3-eye"):
    check_device_token(request)
    rate_limit("llm_local", per_minute=int(__import__("os").getenv(
        "LLM_LOCAL_RATE_PER_MIN", "20")))
    try:
        body = json.loads(await request.body() or b"{}")
    except ValueError:
        raise HTTPException(status_code=400, detail="bad json")
    if not isinstance(body, dict):
        raise HTTPException(status_code=400, detail="bad json")

    user_text = _clean(text or _extract(body))
    if not user_text:
        raise HTTPException(status_code=400, detail="text required")

    plan = plan_text(user_text, use_llm=True)
    persist_plan("home-system", plan, device_id)
    executed = False
    results = None
    if plan.get("actions"):
        # announce=False：播报交给端侧，避免与端侧自己的 announce 重复
        results = execute_plan(plan, device_id=device_id or "esp32s3-eye",
                              announce=False)
        executed = True
    reply = _spoken(plan.get("speak") or "好的")
    logger.info("llm_local dev=%s text=%.40s src=%s type=%s actions=%d",
                device_id, user_text, plan.get("source"),
                plan.get("intent_type"), len(plan.get("actions") or []))
    return {
        "id": plan.get("plan_id"),
        "object": "chat.completion",
        "model": body.get("model") or "homemind-local",
        "choices": [{
            "index": 0,
            "message": {"role": "assistant", "content": reply},
            "finish_reason": "stop",
        }],
        # 便于端侧/小程序展示与排障
        "text": user_text,
        "plan": plan,
        "executed": executed,
        "results": results,
        "source": plan.get("source"),
    }
