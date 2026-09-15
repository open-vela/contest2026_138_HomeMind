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
import re
import time

from fastapi import APIRouter, BackgroundTasks, HTTPException, Request

from .. import local_asr, xiaoai
from ..agent_plan import execute_plan, persist_plan, plan_text
from ..device_auth import check_device_token, rate_limit

logger = logging.getLogger("voice")
router = APIRouter(prefix="/v1/voice", tags=["voice"])

MAX_AUDIO_BYTES = 320 * 1024   # 16k/16bit/mono 约 10 秒，够一句家庭指令

# 唤醒词（"你好，openvela"）在 ASR 结果里的各种可能写法。只剥离句首的那一次 ——
# 端侧把整句（唤醒词 + 指令）一起送上来，规划器只应看到指令部分。
_WAKE_RE = re.compile(
    r"^[\s，,。.、!！?？~〜\-]*"
    r"(?:(?:你好|您好|哈喽|hello|hi|嗨)[\s，,。.、!！]*)?"
    r"(?:open\s*vela|openvela|欧[朋鹏]维拉|欧[朋鹏]微拉|欧朋伟拉)"
    r"[\s，,。.、!！?？~〜\-]*",
    re.IGNORECASE,
)


def strip_wake_word(text: str) -> str:
    """去掉句首的唤醒词，返回剩下的指令部分（可能为空）。"""
    if not text:
        return ""
    return _WAKE_RE.sub("", text, count=1).strip()


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
    heard = local_asr.transcribe(raw, rate) or ""
    asr_ms = int((time.time() - t0) * 1000)
    # 端侧可能把"唤醒词 + 指令"整句一起送上来（唤醒判定发生在人声之后的静音，
    # 那时用户已经把整句说完了）。剥掉句首唤醒词；剥完为空说明用户只叫了一声、
    # 还没讲指令 -> 按 asr_empty 回，端侧会改用"提示音 + 重新录音"再问一次。
    text = strip_wake_word(heard)

    base = {"asr": "local_asr", "asr_ms": asr_ms, "egress": "none"}
    if not text:
        # 没听到有效人话：明确不触发任何控制，也不写入意图历史
        reason = "wake_only" if heard else "asr_empty"
        if reason == "wake_only":
            logger.info("voice utterance dev=%s wake word only, no command",
                        device_id)
        return dict(base, text="", reason=reason, executed=False,
                    actions=0, plan=None)

    plan = plan_text(text, use_llm=True)
    persist_plan("home-system", plan, device_id)
    result = None
    executed = False
    if execute and plan.get("actions"):
        result = execute_plan(plan, device_id=device_id or "esp32s3-eye")
        executed = True
    logger.info("voice utterance dev=%s asr_ms=%d src=%s type=%s "
                "actions=%d text=%s",
                device_id, asr_ms, plan.get("source"),
                plan.get("intent_type"), len(plan.get("actions") or []), text)
    return dict(base, text=text, plan=plan, executed=executed,
                actions=len(plan.get("actions") or []), result=result)


@router.post("/wake")
async def voice_wake(request: Request, background: BackgroundTasks,
                     device_id: str = "esp32s3-eye"):
    """唤醒提示音：端侧检测到唤醒词后调用，让家庭音箱回一句"我在，请说"。

    为什么需要它：用户往往把唤醒词和指令连成一整句说，而唤醒是"人声之后
    出现静音"才判定成功的——等到判定成功，指令已经被说完，端侧再开录音
    只能录到沉默（asr_empty）。先给一个听得见的应答，用户才知道该开口。

    提示音是尽力而为：后台播报，立即返回，端侧无论如何都会继续录音。
    """
    check_device_token(request)
    rate_limit("voice_wake",
               per_minute=int(os.getenv("VOICE_WAKE_RATE_PER_MIN", "30")))
    prompt = os.getenv("VOICE_WAKE_PROMPT", "我在，请说")
    background.add_task(_say_safe, prompt)
    return {"ok": True, "prompt": prompt, "mode": "async"}


def _say_safe(text: str) -> None:
    """后台播报，失败只记日志，绝不影响端侧录音。"""
    try:
        xiaoai.say(text)
    except Exception as exc:            # noqa: BLE001
        logger.warning("wake prompt failed: %s", exc)


@router.get("/health")
def voice_health():
    """无需鉴权的能力自检：便于端侧与运维判断能否走本地语音。"""
    return {
        "local_asr": local_asr.status(),
        "privacy_mode": os.getenv("PRIVACY_MODE", "1") not in (
            "0", "false", "False", ""),
        "note": "音频仅在本机转写，不出局域网",
    }
