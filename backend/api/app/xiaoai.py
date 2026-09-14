"""小爱音箱能力封装（经 HA xiaomi_home）。

已验证/可用实体（多功能房 LX06, did 2085562629）：
- notify...play_text：TTS 播报
- notify...execute_text_directive：执行文本指令，message 必须是 YAML 列表
  [文本内容, 指令静默执行bool]，例如 '["打开多功能房吸顶灯", true]'
- button...wake_up / play_music / play_radio / stop_alarm
- switch...mute（麦克风静音）/ sleep_mode
- media_player：音量/播放控制
边界：不能改唤醒词；不能把麦克风 PCM 读给 HomeMind。
"""
import json
import logging
import os

import httpx

logger = logging.getLogger("xiaoai")


def _ha():
    url = os.getenv("HOMEMIND_HA_URL", "http://127.0.0.1:8123").rstrip("/")
    tok = os.getenv("HOMEMIND_HA_TOKEN", "")
    return url, tok


def _ids():
    return {
        "play_text": "notify.xiaomi_cn_2085562629_lx06_play_text_a_5_1",
        "execute": "notify.xiaomi_cn_2085562629_lx06_execute_text_directive_a_5_5",
        "wake_up": "button.xiaomi_cn_2085562629_lx06_wake_up_a_5_3",
        "play_music": "button.xiaomi_cn_2085562629_lx06_play_music_a_5_2",
        "play_radio": "button.xiaomi_cn_2085562629_lx06_play_radio_a_5_4",
        "stop_alarm": "button.xiaomi_cn_2085562629_lx06_stop_alarm_a_6_1",
        "mute_mic": "switch.xiaomi_cn_2085562629_lx06_mute_p_4_1",
        "sleep_mode": "switch.xiaomi_cn_2085562629_lx06_sleep_mode_p_5_3",
        "media": "media_player.xiaomi_cn_2085562629_lx06",
    }


def _call_service(domain, service, data, timeout=12.0):
    url, tok = _ha()
    if not tok:
        return {"ok": False, "error": "no HA token"}
    r = httpx.post(
        f"{url}/api/services/{domain}/{service}",
        headers={"Authorization": f"Bearer {tok}"},
        json=data, timeout=timeout)
    return {"ok": r.status_code == 200, "status": r.status_code,
            "body": r.text[:300]}


def say(text: str) -> dict:
    """小爱 TTS 播报。"""
    return _call_service("notify", "send_message", {
        "entity_id": _ids()["play_text"], "message": text[:200]})


def execute_directive(text: str, silent: bool = True) -> dict:
    """让小爱执行一句话指令（等价用户对小爱说话后的动作）。

    message 必须是 YAML 列表字符串：'["打开多功能房吸顶灯", true]'
    """
    # 转义内部双引号
    t = text.replace('\\', '\\\\').replace('"', '\\"')
    msg = f'["{t}", {"true" if silent else "false"}]'
    return _call_service("notify", "send_message", {
        "entity_id": _ids()["execute"], "message": msg})


def press_button(name: str) -> dict:
    ent = _ids().get(name)
    if not ent or not ent.startswith("button."):
        return {"ok": False, "error": f"unknown button {name}"}
    return _call_service("button", "press", {"entity_id": ent})


def set_mute(on: bool) -> dict:
    return _call_service("switch", "turn_on" if on else "turn_off",
                         {"entity_id": _ids()["mute_mic"]})


def set_sleep_mode(on: bool) -> dict:
    return _call_service("switch", "turn_on" if on else "turn_off",
                         {"entity_id": _ids()["sleep_mode"]})


def volume_set(level: float) -> dict:
    level = max(0.0, min(1.0, float(level)))
    return _call_service("media_player", "volume_set", {
        "entity_id": _ids()["media"], "volume_level": level})


def list_capabilities() -> list:
    return [
        {"name": "say", "desc": "TTS 播报", "entity": _ids()["play_text"]},
        {"name": "execute_directive", "desc": "执行文本指令（关灯等）",
         "entity": _ids()["execute"],
         "format": '["指令文本", silent_bool]'},
        {"name": "wake_up", "desc": "程序唤醒小爱", "entity": _ids()["wake_up"]},
        {"name": "play_music", "desc": "播放音乐", "entity": _ids()["play_music"]},
        {"name": "play_radio", "desc": "播放电台", "entity": _ids()["play_radio"]},
        {"name": "stop_alarm", "desc": "停止闹钟", "entity": _ids()["stop_alarm"]},
        {"name": "set_mute", "desc": "麦克风静音开关", "entity": _ids()["mute_mic"]},
        {"name": "set_sleep_mode", "desc": "睡眠模式开关", "entity": _ids()["sleep_mode"]},
        {"name": "volume_set", "desc": "音量 0-1", "entity": _ids()["media"]},
        {"limit": "自定义唤醒词 / 麦克风 PCM 回读：不支持"},
    ]
