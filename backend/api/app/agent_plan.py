"""家庭私有云语义规划：复杂意图 → 白名单结构化 JSON → 可选执行。

端侧只上报文本（或已转写文本）；原始音视频默认不出家庭。
LLM 仅在家庭主机调用；失败时回落本地规则，保证断网/无 Key 仍可用。
输出指令必须落在 ALLOWED_ACTIONS / 已知场景动作内，未知动作 rejected。
"""
import json
import logging
import os
import re
import uuid
from datetime import datetime

import httpx

from .config import settings
from .db import SessionLocal
from .models import Command, HomeEvent, Intent
from .mqtt_client import publish_command
from . import local_llm

logger = logging.getLogger("agent_plan")

# 结构化计划 schema（给 ESP32/网关/场景执行器）
# {
#   "plan_id": "...",
#   "intent_type": "bedtime|device.control|task.create|unknown",
#   "actions": [
#     {"type":"scene","name":"bedtime"},
#     {"type":"led","op":"off"},
#     {"type":"xiaoai_execute","text":"关闭多功能房吸顶灯"},
#     {"type":"speak","text":"..."},
#     {"type":"task","title":"...","minutes":15}
#   ],
#   "speak": "...",
#   "source": "rules|mimo",
#   "rejected": []
# }


def _now():
    return datetime.utcnow()


def _mimo_key():
    return os.getenv("MIMO_API_KEY", "")


def _call_mimo(text: str, timeout: float = 25.0) -> dict | None:
    key = _mimo_key()
    if not key:
        return None
    model = os.getenv("MIMO_MODEL", "mimo-v2.5")
    sys = (
        "你是家庭中枢语义解析器。把用户中文指令解析成 JSON，不要解释。"
        "只允许这些 action.type："
        "scene(name=bedtime), led(op=on|off), "
        "xiaoai_execute(text=对小爱说的中文指令), speak(text=短播报), "
        "task(title,minutes)。"
        "未知或危险操作放进 rejected。"
        "输出单个 JSON 对象："
        '{"intent_type":"...","actions":[...],"speak":"..."}'
    )
    body = {
        "model": model,
        "messages": [
            {"role": "system", "content": sys},
            {"role": "user", "content": text[:500]},
        ],
        "temperature": 0.2,
    }
    try:
        r = httpx.post(
            os.getenv("MIMO_API_BASE", "https://api.xiaomimimo.com")
            + "/v1/chat/completions",
            json=body,
            headers={"Authorization": "Bearer " + key},
            timeout=timeout,
        )
        if r.status_code != 200:
            logger.warning("mimo status=%s", r.status_code)
            return None
        content = r.json()["choices"][0]["message"]["content"] or ""
        m = re.search(r"\{[\s\S]*\}", content)
        if not m:
            return None
        return json.loads(m.group(0))
    except Exception as e:
        logger.warning("mimo plan failed: %s", e)
        return None


# 家庭私有大模型的系统提示：强约束 JSON，带少量示例提升小模型稳定性。
LOCAL_LLM_SYSTEM = """你是 HomeMind 家庭中枢的语义规划器，运行在家庭局域网私有云上。
把用户的中文指令拆解成可执行的动作列表，只输出一个 JSON 对象，不要任何解释。

可用 action.type（只能用这些）：
1) {"type":"scene","name":"bedtime"}  执行睡前场景（仅 bedtime/sleep/good_night）
2) {"type":"led","op":"on"}           板载 LED，op 只能是 on 或 off
3) {"type":"xiaoai_execute","text":"对小爱音箱说的中文指令"}  控制米家设备
4) {"type":"speak","text":"要播报的简短中文"}                语音回复用户
5) {"type":"task","title":"待办标题","minutes":30}            创建提醒待办

输出格式：
{"intent_type":"bedtime|device.control|task.create|greeting|unknown","actions":[...],"speak":"一句话回复"}

规则：
- 一个意图可以拆成多个动作，按执行顺序排列。
- text/title 必须填**用户真正想做的具体内容**，绝对不要照抄下面示例里的占位文字。
- 用户说的是开发板/板载/指示灯，就用 led；说家用灯/吸顶灯/房间灯，才用 xiaoai_execute。
- 不要执行危险或非家庭操作（如转账、删数据、开门锁），把它们输出为 {"intent_type":"unknown","actions":[],"speak":"这个我做不了"}。
- speak 要简短自然，不超过 30 个汉字。

示例：
用户：我准备睡觉了
输出：{"intent_type":"bedtime","actions":[{"type":"scene","name":"bedtime"},{"type":"led","op":"off"},{"type":"speak","text":"晚安，已帮你进入睡前模式"}],"speak":"晚安，已帮你进入睡前模式"}

用户：把灯关了
输出：{"intent_type":"device.control","actions":[{"type":"xiaoai_execute","text":"关闭多功能房吸顶灯"},{"type":"led","op":"off"}],"speak":"好的，灯已关"}

用户：把开发板上的指示灯打开
输出：{"intent_type":"device.control","actions":[{"type":"led","op":"on"}],"speak":"好的，板载灯已打开"}

用户：二十分钟后提醒我收衣服
输出：{"intent_type":"task.create","actions":[{"type":"task","title":"收衣服","minutes":20},{"type":"speak","text":"好的，20分钟后提醒你收衣服"}],"speak":"好的，20分钟后提醒你收衣服"}
"""


def _allow_external_llm() -> bool:
    """是否允许把文本发到家庭之外的大模型。隐私模式下禁止。"""
    return os.getenv("PRIVACY_ALLOW_EXTERNAL_LLM", "0") not in (
        "0", "false", "False", "")


def _call_local_llm(text: str) -> dict | None:
    """家庭私有大模型规划：数据不出局域网。"""
    if not local_llm.enabled():
        return None
    timeout = float(os.getenv("LOCAL_LLM_TIMEOUT", "45"))
    data = local_llm.chat_json(LOCAL_LLM_SYSTEM, (text or "")[:300],
                               timeout=timeout)
    if not isinstance(data, dict):
        return None
    if "actions" not in data and "intent_type" not in data:
        return None
    return data


def local_rules(text: str) -> dict:
    """无网/无 Key 回落：关键词规则。"""
    t = (text or "").strip()
    tl = t.lower()
    if any(k in t for k in ("睡觉", "睡了", "晚安", "就寝")) or "good night" in tl:
        return {
            "intent_type": "bedtime",
            "actions": [
                {"type": "scene", "name": "bedtime"},
            ],
            "speak": "好的，开始睡前准备。",
            "source": "rules",
        }
    # 板载指示灯（端侧 LED）：确定性最高，且不触碰家里真实灯具，优先匹配
    if any(k in t for k in ("指示灯", "板载", "板上的灯", "开发板上的灯")) \
            or tl in ("led",) or " led" in tl:
        off = any(k in t for k in ("关", "灭", "关闭", "熄"))
        return {
            "intent_type": "device.control",
            "actions": [{"type": "led", "op": "off" if off else "on"}],
            "speak": "好的，板载灯已关闭。" if off else "好的，板载灯已打开。",
            "source": "rules",
        }
    if any(k in t for k in ("关灯", "把灯关了", "关闭灯")):
        return {
            "intent_type": "device.control",
            "actions": [
                {"type": "xiaoai_execute", "text": "关闭多功能房吸顶灯"},
                {"type": "led", "op": "off"},
            ],
            "speak": "好的，关灯。",
            "source": "rules",
        }
    if any(k in t for k in ("开灯", "打开灯", "把灯打开")):
        return {
            "intent_type": "device.control",
            "actions": [
                {"type": "xiaoai_execute", "text": "打开多功能房吸顶灯"},
                {"type": "led", "op": "on"},
            ],
            "speak": "好的，开灯。",
            "source": "rules",
        }
    if t.startswith("提醒") or "提醒我" in t:
        return {
            "intent_type": "task.create",
            "actions": [
                {"type": "task", "title": t[:80], "minutes": 30},
            ],
            "speak": "已记下待办提醒。",
            "source": "rules",
        }
    return {
        "intent_type": "unknown",
        "actions": [],
        "speak": "我没有听懂，可以说：我准备睡觉了。",
        "source": "rules",
        "rejected": [t[:80]],
    }


# ---- 语义安全护栏 ----------------------------------------------------------
# 白名单只校验 action.type，挡不住"类型合法但语义危险"的输出。
# 小模型（1.5B）安全对齐不足，实测会把"转账"解析成 xiaoai_execute 执行掉。
# 因此在执行前再做一次风险词兜底，命中即整体拒绝（fail-closed）。
RISK_KEYWORDS = (
    # 资金与凭据
    "转账", "汇款", "付款", "支付", "充值", "提现", "打钱", "借钱", "贷款",
    "银行卡", "信用卡", "余额", "理财", "股票", "基金", "投钱",
    "密码", "验证码", "口令", "密钥", "身份证", "护照", "社保",
    # 物理安全
    "开门", "开锁", "解锁", "门禁", "大门", "窗户", "撤防", "解除警报",
    "关闭监控", "关掉监控", "关闭摄像头",
    # 破坏性操作
    "删除", "格式化", "清空", "重置", "恢复出厂", "刷机", "关机", "断电",
    # 外发隐私
    "发给", "转发给", "分享给", "上传到",
)


def _risk_hit(text: str):
    """返回命中的风险词，未命中返回 None。"""
    for k in RISK_KEYWORDS:
        if k in (text or ""):
            return k
    return None


# 小模型有时会把系统提示里的 schema 占位文字当成真实内容输出，实测 1.5B 会返回
# {"type":"xiaoai_execute","text":"对小爱音箱说的中文指令"}。这类模板文字一旦执行，
# 就会把示例串真的发给小爱或写进待办，必须在执行前拦掉。
PLACEHOLDER_TEXTS = (
    "对小爱音箱说的中文指令", "对小爱说的中文指令", "要播报的简短中文",
    "一句话回复", "待办标题", "待办", "指令", "标题", "text", "title", "...", "…",
)

_PUNCT = re.compile(r"[\s，。、,.!！?？\"'“”‘’：:；;（）()\[\]【】]")


def _is_placeholder(text: str) -> bool:
    """text 为空或只是系统提示里的占位样例 -> 视为无效内容。"""
    t = _PUNCT.sub("", str(text or "")).lower()
    if not t:
        return True
    return any(t == _PUNCT.sub("", p).lower() for p in PLACEHOLDER_TEXTS)


def sanitize_plan(raw: dict, user_text: str = "") -> dict:
    """过滤非法动作，保证只执行白名单/已知类型；并做风险语义兜底。"""
    # 护栏同时检查"用户原话"和"模型输出"，防止模型把危险意图洗白成合规动作
    blob = " ".join([
        str(user_text or ""),
        str(raw.get("speak") or ""),
        " ".join(str(a.get("text") or "") + str(a.get("title") or "")
                 for a in (raw.get("actions") or []) if isinstance(a, dict)),
    ])
    hit = _risk_hit(blob)
    if hit:
        logger.warning("plan blocked by safety guard, keyword=%s", hit)
        return {
            "plan_id": uuid.uuid4().hex,
            "intent_type": "unsafe",
            "actions": [],
            "speak": f"出于安全考虑，我不会执行涉及「{hit}」的操作。",
            "source": raw.get("source") or "rules",
            "rejected": [f"blocked_by_guard:{hit}"],
        }

    allowed_types = {"scene", "led", "xiaoai_execute", "speak", "task"}
    actions = []
    rejected = list(raw.get("rejected") or [])
    for a in (raw.get("actions") or []):
        if not isinstance(a, dict):
            rejected.append(str(a)[:80])
            continue
        t = a.get("type")
        if t not in allowed_types:
            rejected.append(str(a)[:120])
            continue
        if t == "led" and a.get("op") not in ("on", "off"):
            rejected.append(str(a)[:120])
            continue
        if t == "scene" and a.get("name") not in ("bedtime", "sleep", "good_night"):
            rejected.append(str(a)[:120])
            continue
        if t == "task":
            try:
                a["minutes"] = max(1, min(int(a.get("minutes", 30)), 24 * 60))
            except Exception:
                a["minutes"] = 30
            title = str(a.get("title") or "")[:120]
            if _is_placeholder(title) or title == "待办":
                rejected.append(f"placeholder_title:{title[:40]}")
                continue
            a["title"] = title
        if t == "xiaoai_execute":
            a["text"] = str(a.get("text") or "")[:80]
            if _is_placeholder(a["text"]):
                # 模型照抄了示例文字，不能真的发给小爱
                rejected.append(f"placeholder_text:{a['text'][:40]}")
                continue
        if t == "speak":
            a["text"] = str(a.get("text") or "")[:120]
            if _is_placeholder(a["text"]):
                rejected.append("placeholder_speak")
                continue
        actions.append(a)
    return {
        "plan_id": uuid.uuid4().hex,
        "intent_type": raw.get("intent_type") or "unknown",
        "actions": actions,
        "speak": str(raw.get("speak") or "")[:160],
        "source": raw.get("source") or "rules",
        "rejected": rejected,
    }


def plan_text(text: str, use_llm: bool = True) -> dict:
    """三级规划：本地私有大模型 -> （可选）外部大模型 -> 关键词规则。

    优先级刻意把"家庭本地模型"放在最前：默认不出局域网，符合隐私主张。
    外部大模型只有在显式打开 PRIVACY_ALLOW_EXTERNAL_LLM 且配置了 Key 时才用。
    """
    raw = None
    src = "rules"
    if use_llm:
        raw = _call_local_llm(text)
        if raw:
            raw["source"] = "local_llm"
            src = "local_llm"
        if not raw and _allow_external_llm() and _mimo_key():
            raw = _call_mimo(text)
            if raw:
                raw["source"] = "mimo"
                src = "mimo"
    if not raw:
        raw = local_rules(text)
        src = "rules"
    plan = sanitize_plan(raw, user_text=text)
    plan["source"] = src
    plan["input_text"] = (text or "")[:200]
    return plan


def persist_plan(user_id: str, plan: dict, device_id: str) -> dict:
    db = SessionLocal()
    try:
        it = Intent(
            intent_id=plan["plan_id"], user_id=user_id or "home-system",
            text=plan.get("input_text", ""),
            intent_type=plan.get("intent_type", ""),
            action="agent.plan",
            entities=json.dumps({"plan": plan}, ensure_ascii=False),
            params="{}",
            status="handled" if plan.get("actions") else "rejected",
            device_id=device_id or "",
            reason=";".join(plan.get("rejected") or [])[:200],
            created_at=_now(), handled_at=_now())
        db.add(it)
        db.add(HomeEvent(
            event_id=uuid.uuid4().hex, device_id=device_id or "",
            source="agent", event_type="plan_created",
            payload=json.dumps({
                "plan_id": plan["plan_id"],
                "intent_type": plan.get("intent_type"),
                "actions": plan.get("actions"),
                "source": plan.get("source"),
            }, ensure_ascii=False),
            created_at=_now()))
        db.commit()
        return plan
    finally:
        db.close()


def execute_plan(plan: dict, device_id: str = "esp32s3-eye",
                 announce: bool = True) -> dict:
    """执行计划：scene / led / xiaoai / speak / task。返回逐项结果。

    announce=False 时不调用小爱播报（speak 动作标记为 skipped），用于「端侧自己
    播报回复」的语音闭环，避免同一条回复被播两遍。
    """
    results = []
    # 延迟导入避免循环
    from .routers import scenes as scenes_mod
    from . import xiaoai

    for a in plan.get("actions") or []:
        t = a.get("type")
        try:
            if t == "scene":
                db = SessionLocal()
                try:
                    out = scenes_mod._plan_bedtime(
                        db, "home-system", device_id, "", False)
                finally:
                    db.close()
                results.append({"action": a, "ok": True, "result": {
                    "actions": out.get("actions"),
                    "tasks": len(out.get("tasks") or []),
                    "speak": out.get("speak"),
                }})
            elif t == "led":
                op = a.get("op")
                act = "led.on" if op == "on" else "led.off"
                if act not in settings.ALLOWED_ACTIONS:
                    results.append({"action": a, "ok": False,
                                    "error": "not allowed"})
                    continue
                db = SessionLocal()
                try:
                    cid = uuid.uuid4().hex
                    db.add(Command(
                        command_id=cid, user_id="home-system",
                        device_id=device_id, action=act,
                        params=json.dumps({"reason": "agent_plan"}),
                        ttl=30, status="queued", created_at=_now()))
                    db.commit()
                    publish_command(f"device/{device_id}/cmd", {
                        "command_id": cid, "action": act,
                        "params": {"reason": "agent_plan"}, "ttl": 30,
                        "ts": int(_now().timestamp())})
                    results.append({"action": a, "ok": True, "command_id": cid})
                finally:
                    db.close()
            elif t == "xiaoai_execute":
                r = xiaoai.execute_directive(a.get("text", ""), silent=True)
                results.append({"action": a, "ok": bool(r.get("ok")),
                                "result": r})
            elif t == "speak":
                if not announce:
                    results.append({"action": a, "ok": True,
                                    "skipped": "announce disabled"})
                    continue
                r = xiaoai.say(a.get("text", ""))
                results.append({"action": a, "ok": bool(r.get("ok")),
                                "result": r})
            elif t == "task":
                db = SessionLocal()
                try:
                    from datetime import timedelta
                    tid = uuid.uuid4().hex
                    from .models import Task
                    due = _now() + timedelta(minutes=int(a.get("minutes", 30)))
                    db.add(Task(
                        task_id=tid, user_id="home-system",
                        title=a.get("title", "待办"),
                        note="agent_plan", due_at=due,
                        timezone="Asia/Shanghai", remind_at=due,
                        status="pending", created_at=_now()))
                    db.commit()
                    results.append({"action": a, "ok": True, "task_id": tid})
                finally:
                    db.close()
            else:
                results.append({"action": a, "ok": False, "error": "unknown"})
        except Exception as e:
            logger.error("execute plan action failed: %s", e)
            results.append({"action": a, "ok": False, "error": str(e)})

    # 顶层 speak（若 actions 里没有 speak）
    if announce and plan.get("speak") and not any(
            a.get("type") == "speak" for a in plan.get("actions") or []):
        try:
            from . import xiaoai
            r = xiaoai.say(plan["speak"])
            results.append({"action": {"type": "speak", "text": plan["speak"]},
                            "ok": bool(r.get("ok")), "result": r})
        except Exception as e:
            results.append({"action": {"type": "speak"}, "ok": False,
                            "error": str(e)})
    return {"plan_id": plan.get("plan_id"), "results": results}
