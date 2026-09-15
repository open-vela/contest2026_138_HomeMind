"""端侧语音闭环 API：唤醒后的 PCM -> 局域网本地 ASR -> 语义规划 -> 执行。

POST /v1/voice/utterance?rate=16000&device_id=esp32s3-eye   body=16bit 单声道 PCM
  -> {"text": 转写文本, "plan": {...}, "executed": true, "result": {...}}
GET  /v1/voice/health  本地 ASR 能力自检

相比 /v1/media/audio（隐私模式下 403、非隐私模式走第三方 MiMo）这条路径：
本端点把音频转写**完全放在家庭私有云本机**，音频不出局域网、不经过任何第三方，
因此可以在 PRIVACY_MODE=1 下正常服务——这正是"断网也能用、隐私绝对安全"的落点。
识别不到人话时返回 reason=asr_empty 且 executed=false，绝不误触发控制。
"""
import logging
import os
import time

from fastapi import APIRouter, HTTPException, Request

from .. import local_asr
from ..agent_plan import execute_plan, persist_plan, plan_text
from ..device_auth import check_device_token, rate_limit

logger = logging.getLogger("voice")
router = APIRouter(prefix="/v1/voice", tags=["voice"])

MAX_AUDIO_BYTES = 320 * 1024   # 16k/16bit/mono 约 10 秒，够一句家庭指令


@router.post("/utterance")
async def voice_utterance(request: Request, rate: int = 16000,
                          device_id: str = "esp32s3-eye", execute: int = 1):
    check_device_token(request)
    rate_limit("voice", per_minute=int(os.getenv("VOICE_RATE_PER_MIN", "20")))
    if not (8000 <= rate <= 48000):
        raise HTTPException(status_code=400, detail="bad rate")
    raw = await request.body()
    if not raw or len(raw) > MAX_AUDIO_BYTES:
        raise HTTPException(status_code=400, detail="bad audio size")

    t0 = time.time()
    text = local_asr.transcribe(raw, rate)
    asr_ms = int((time.time() - t0) * 1000)

    base = {"asr": "local_asr", "asr_ms": asr_ms, "egress": "none"}
    if not text:
        # 没听到有效人话：明确不触发任何控制，也不写入意图历史
        return dict(base, text="", reason="asr_empty", executed=False,
                    actions=0, plan=None)

    plan = plan_text(text, use_llm=True)
    persist_plan("home-system", plan, device_id)
    result = None
    executed = False
    if execute and plan.get("actions"):
        result = execute_plan(plan, device_id=device_id or "esp32s3-eye")
        executed = True
    logger.info("voice utterance dev=%s asr_ms=%d src=%s type=%s actions=%d",
                device_id, asr_ms, plan.get("source"),
                plan.get("intent_type"), len(plan.get("actions") or []))
    return dict(base, text=text, plan=plan, executed=executed,
                actions=len(plan.get("actions") or []), result=result)


@router.get("/health")
def voice_health():
    """无需鉴权的能力自检：便于端侧与运维判断能否走本地语音。"""
    return {
        "local_asr": local_asr.status(),
        "privacy_mode": os.getenv("PRIVACY_MODE", "1") not in (
            "0", "false", "False", ""),
        "note": "音频仅在本机转写，不出局域网",
    }
