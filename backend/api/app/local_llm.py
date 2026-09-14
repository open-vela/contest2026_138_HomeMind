"""本地私有大模型（端云协同的"云"侧大脑）。

设计原则（对应方案第 2、4 点）：
- 模型与推理全部跑在家庭局域网主机上，原始音视频与文本不出家庭；
- 只在需要"复杂意图拆解"时调用；关键词能覆盖的走本地规则，保证低延迟；
- 任何失败（模型缺失/超时/输出非法）都必须静默回落到规则，不能让家庭控制失效。

实现：llama-cpp-python + Qwen2.5-1.5B-Instruct GGUF（CPU 推理）。
"""
import json
import logging
import os
import re
import threading

logger = logging.getLogger("local_llm")

_llm = None
_lock = threading.Lock()
_load_failed = False

DEFAULT_MODEL = "/home/hfy/homemind-models/qwen2.5-1.5b-instruct-q4_k_m.gguf"


def model_path() -> str:
    return os.getenv("LOCAL_LLM_MODEL", DEFAULT_MODEL)


def enabled() -> bool:
    """是否启用本地大模型：显式开关 + 模型文件存在 + 依赖可用。"""
    if os.getenv("LOCAL_LLM_ENABLED", "1") in ("0", "false", "False", ""):
        return False
    p = model_path()
    if not p or not os.path.exists(p):
        return False
    try:
        import llama_cpp  # noqa: F401
        return True
    except Exception:
        return False


def get_llm():
    """懒加载模型（首次约数秒），失败后不再重试，避免每次请求都卡顿。"""
    global _llm, _load_failed
    if _llm is not None or _load_failed:
        return _llm
    with _lock:
        if _llm is not None or _load_failed:
            return _llm
        try:
            from llama_cpp import Llama
            n_threads = int(os.getenv("LOCAL_LLM_THREADS", "4"))
            _llm = Llama(
                model_path=model_path(),
                n_ctx=int(os.getenv("LOCAL_LLM_CTX", "2048")),
                n_threads=n_threads,
                n_gpu_layers=0,      # 纯 CPU，家庭主机通常无可用 GPU
                verbose=False,
            )
            logger.info("local llm loaded: %s threads=%d", model_path(), n_threads)
        except Exception as e:
            _load_failed = True
            logger.warning("local llm load failed: %s", e)
    return _llm


_FENCE = re.compile(r"```(?:json)?\s*([\s\S]*?)```", re.I)


def _extract_json(text: str):
    """从模型输出里尽力抠出第一个 JSON 对象。"""
    if not text:
        return None
    s = text.strip()
    m = _FENCE.search(s)
    if m:
        s = m.group(1).strip()
    # 直接整体解析
    try:
        return json.loads(s)
    except Exception:
        pass
    # 截取第一个 {...} 块（尽量贪婪到最后一个右括号再回退）
    i, j = s.find("{"), s.rfind("}")
    while i >= 0 and j > i:
        try:
            return json.loads(s[i:j + 1])
        except Exception:
            j = s.rfind("}", 0, j)
    return None


def chat_json(system: str, user: str, max_tokens: int = 320,
              timeout: float = 60.0):
    """返回解析后的 dict；失败返回 None。同步阻塞，请在线程池中调用。"""
    if not enabled():
        return None
    llm = get_llm()
    if llm is None:
        return None
    temp = float(os.getenv("LOCAL_LLM_TEMP", "0.2"))
    # 用线程池实现硬超时：llama-cpp 自身没有超时参数
    from concurrent.futures import ThreadPoolExecutor
    pool = ThreadPoolExecutor(max_workers=1)

    def _run():
        return llm.create_chat_completion(
            messages=[{"role": "system", "content": system},
                      {"role": "user", "content": user}],
            temperature=temp,
            max_tokens=max_tokens,
        )

    try:
        fut = pool.submit(_run)
        out = fut.result(timeout=timeout)
        content = (out or {}).get("choices", [{}])[0].get("message", {}).get("content") or ""
        data = _extract_json(content)
        if data is None:
            logger.warning("local llm non-json output: %.200s", content)
        return data
    except Exception as e:
        logger.warning("local llm inference failed: %s", e)
        return None
    finally:
        pool.shutdown(wait=False)


def status() -> dict:
    return {
        "enabled": enabled(),
        "model_path": model_path() if os.getenv("LOCAL_LLM_ENABLED", "1") not in ("0",) else "",
        "model_exists": os.path.exists(model_path()),
        "loaded": _llm is not None,
        "load_failed": _load_failed,
    }
