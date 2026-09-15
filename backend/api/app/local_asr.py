"""家庭局域网内本地语音识别（ASR）——唤醒后语音闭环的"听得懂"环节。

设计原则（对应方案第 1、2、4 点）：
- 模型与推理全部在家庭私有云本机，音频**不出局域网**、不经过任何第三方；
- 端侧 KWS 唤醒后采集的 PCM 直接上行到私有云本地转写，转写文本再交给
  agent_plan 做语义规划，全程无公网依赖；
- 与 local_llm 一致：任何失败都静默返回 None，由调用方决定回落策略，
  绝不让"语音识别"这一环把整个家庭控制拖死。

实现：faster-whisper（CTranslate2）+ whisper-small 中文模型，CPU int8 推理。
模型目录默认为 /home/hfy/homemind-models/faster-whisper-small（ModelScope 获取）。
"""
import logging
import os
import re
import struct
import tempfile
import threading

logger = logging.getLogger("local_asr")

DEFAULT_MODEL_DIR = "/home/hfy/homemind-models/faster-whisper-small"

_model = None
_lock = threading.Lock()
_load_failed = False


def model_dir() -> str:
    return os.getenv("ASR_MODEL_DIR", DEFAULT_MODEL_DIR)


def enabled() -> bool:
    """显式开关 + 模型文件齐全 + 依赖可用。"""
    if os.getenv("LOCAL_ASR_ENABLED", "1") in ("0", "false", "False", ""):
        return False
    d = model_dir()
    if not d or not os.path.exists(os.path.join(d, "model.bin")):
        return False
    try:
        import faster_whisper  # noqa: F401
        return True
    except Exception:
        return False


def get_model():
    """懒加载（首次约 1-2 秒），失败后不再重试，避免每次请求都卡。"""
    global _model, _load_failed
    if _model is not None or _load_failed:
        return _model
    with _lock:
        if _model is not None or _load_failed:
            return _model
        try:
            from faster_whisper import WhisperModel
            # 避免任何联网探测：模型已完整落盘，tokenizer 也在本地
            os.environ.setdefault("HF_HUB_OFFLINE", "1")
            _model = WhisperModel(
                model_dir(),
                device="cpu",
                compute_type=os.getenv("ASR_COMPUTE_TYPE", "int8"),
                cpu_threads=int(os.getenv("ASR_THREADS", "4")),
                local_files_only=True,
            )
            logger.info("local asr loaded: %s", model_dir())
        except Exception as e:
            _load_failed = True
            logger.warning("local asr load failed: %s", e)
    return _model


def _rms(pcm: bytes) -> float:
    """16bit 单声道 PCM 的均方根，用于判静音，避免模型对静音产生幻觉。"""
    n = len(pcm) // 2
    if n <= 0:
        return 0.0
    try:
        import numpy as np
        a = np.frombuffer(pcm[: n * 2], dtype="<i2").astype("float32")
        return float((a * a).mean() ** 0.5)
    except Exception:
        total = 0
        for (v,) in struct.iter_unpack("<h", pcm[: n * 2]):
            total += v * v
        return (total / n) ** 0.5


def _pcm_to_wav(pcm: bytes, rate: int) -> bytes:
    byte_rate = rate * 2
    header = b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVE"
    header += b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, rate, byte_rate, 2, 16)
    header += b"data" + struct.pack("<I", len(pcm))
    return header + pcm


# whisper 对静音/噪声常见的固定幻觉输出，命中即视为"没听到人话"
_HALLUCINATION = re.compile(
    r"^(字幕|字幕由|请不吝|感谢观看|谢谢观看|订阅|点赞|关注|"
    r"明镜与点点|MING PAO|amara\.org|Thanks for watching|"
    r"www\.|https?://|\.{3,}|。{3,}|\s*)$",
    re.I,
)


def transcribe(pcm: bytes, rate: int = 16000, lang: str = "zh",
               min_rms: float = None, max_wait: float = None):
    """PCM(16bit/mono) -> 文本。无有效语音或识别失败返回 None（调用方回落）。"""
    if not enabled():
        return None
    if len(pcm) < rate * 2 * 0.4:          # 少于 0.4 秒，不可能是完整指令
        return None
    thr = float(os.getenv("ASR_MIN_RMS", "180")) if min_rms is None else min_rms
    if _rms(pcm) < thr:
        logger.info("local asr: silence (rms<%.0f), skipped", thr)
        return None
    model = get_model()
    if model is None:
        return None

    wait = float(os.getenv("ASR_TIMEOUT", "45")) if max_wait is None else max_wait
    # 提示词引导简体中文输出，减少繁体/英文漂移
    prompt = os.getenv("ASR_INITIAL_PROMPT", "以下是普通话的句子。")
    path = None
    try:
        fd, path = tempfile.mkstemp(suffix=".wav", prefix="hm_asr_")
        with os.fdopen(fd, "wb") as f:
            f.write(_pcm_to_wav(pcm, rate))
        segments, _info = model.transcribe(
            path,
            language=lang,
            beam_size=int(os.getenv("ASR_BEAM", "1")),
            vad_filter=True,                 # 去掉首尾静音，显著降幻觉
            condition_on_previous_text=False,
            initial_prompt=prompt,
        )
        text = "".join(s.text for s in segments).strip()
    except Exception as e:
        logger.warning("local asr inference failed: %s", e)
        return None
    finally:
        if path and os.path.exists(path):
            try:
                os.unlink(path)
            except OSError:
                pass

    if not text or _HALLUCINATION.match(text):
        logger.info("local asr: no speech / hallucination filtered: %.60s", text)
        return None
    return text


def status() -> dict:
    return {
        "enabled": enabled(),
        "model_dir": model_dir(),
        "model_exists": os.path.exists(os.path.join(model_dir(), "model.bin")),
        "loaded": _model is not None,
        "load_failed": _load_failed,
        "egress": "none",   # 音频不出局域网
    }
