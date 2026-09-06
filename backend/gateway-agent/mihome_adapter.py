"""安全的米家桥接适配层。

The official Xiaomi Home Integration runs in Home Assistant and exposes the
device state/service boundary. HomeMind keeps the ESP32 unaware of Xiaomi
credentials: this adapter is an optional gateway-side client for a user-owned
Home Assistant instance.
"""

from __future__ import annotations

import json
import os
import re
import time
from typing import Callable, Iterable, Optional
from urllib.error import HTTPError, URLError
from urllib.parse import quote
from urllib.request import Request, urlopen

# Xiaomi/MiOT entities may lag behind a service call before HA reflects the
# new state; poll briefly instead of failing on the first read. BLE/cloud
# devices (e.g. Linp wall switches) routinely take 5-8 s to apply a command,
# and the xiaomi_home integration may even answer the service call with a 500
# "设备操作超时" *before* the device executes it — so the service-call result is
# never trusted on its own: state polling is the single source of truth.
_CONFIRM_WINDOW_S = 10.0
_CONFIRM_INTERVAL_S = 0.5


_ENTITY_RE = re.compile(r"^(light|switch)\.[A-Za-z0-9_]+$")
_POWER_ACTIONS = {"mihome.set_power", "mihome.get_state"}


class MiHomeError(RuntimeError):
    """A safe, user-facing MiHome adapter failure."""


class HomeAssistantMiHomeAdapter:
    """Call only allowlisted Home Assistant light/switch entities.

    The opener is injectable for tests and is deliberately the only HTTP
    boundary. No token is included in exceptions or loggable return values.
    """

    def __init__(
        self,
        base_url: str = "",
        token: str = "",
        allowed_entities: Iterable[str] = (),
        timeout: float = 8.0,
        opener: Optional[Callable[..., object]] = None,
    ):
        self.base_url = base_url.rstrip("/")
        self.token = token
        self.allowed_entities = frozenset(
            entity for entity in allowed_entities if self.valid_entity_id(entity)
        )
        self.timeout = timeout
        self._opener = opener or urlopen

    @classmethod
    def from_env(cls) -> "HomeAssistantMiHomeAdapter":
        entities = os.getenv("HOMEMIND_MIHOME_ALLOWED_ENTITIES", "")
        return cls(
            base_url=os.getenv("HOMEMIND_HA_URL", ""),
            token=os.getenv("HOMEMIND_HA_TOKEN", ""),
            allowed_entities=(item.strip() for item in entities.split(",")),
        )

    @staticmethod
    def valid_entity_id(entity_id: object) -> bool:
        return isinstance(entity_id, str) and bool(_ENTITY_RE.fullmatch(entity_id))

    @property
    def enabled(self) -> bool:
        return bool(self.base_url and self.token and self.allowed_entities)

    def describe(self) -> dict:
        """Return diagnostics without returning the bearer token."""
        return {
            "enabled": self.enabled,
            "allowed_entities": len(self.allowed_entities),
        }

    def _check_entity(self, entity_id: object) -> str:
        if not self.enabled:
            raise MiHomeError(
                "MiHome adapter disabled; set HOMEMIND_HA_URL, "
                "HOMEMIND_HA_TOKEN and an entity allowlist"
            )
        if not self.valid_entity_id(entity_id):
            raise MiHomeError("entity_id must be a light.* or switch.* entity")
        if entity_id not in self.allowed_entities:
            raise MiHomeError("entity_id is not in the MiHome allowlist")
        return entity_id

    def _request(self, method: str, path: str, payload: Optional[dict] = None):
        if not self.enabled:
            raise MiHomeError("MiHome adapter is disabled")
        data = None
        headers = {"Authorization": "Bearer " + self.token}
        if payload is not None:
            data = json.dumps(payload, separators=(",", ":")).encode("utf-8")
            headers["Content-Type"] = "application/json"
        request = Request(
            self.base_url + path,
            data=data,
            headers=headers,
            method=method,
        )
        try:
            response = self._opener(request, timeout=self.timeout)
            raw = response.read()
        except (HTTPError, URLError, OSError) as exc:
            raise MiHomeError("Home Assistant request failed") from exc
        if not raw:
            return None
        try:
            return json.loads(raw.decode("utf-8"))
        except (AttributeError, UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise MiHomeError("Home Assistant returned invalid JSON") from exc

    def get_state(self, entity_id: str) -> dict:
        entity_id = self._check_entity(entity_id)
        result = self._request(
            "GET", "/api/states/" + quote(entity_id, safe=".")
        )
        if not isinstance(result, dict) or result.get("entity_id") != entity_id:
            raise MiHomeError("Home Assistant state response did not match entity")
        return {
            "entity_id": entity_id,
            "state": result.get("state"),
            "attributes": result.get("attributes", {}),
        }

    def set_power(self, entity_id: str, enabled: bool) -> dict:
        entity_id = self._check_entity(entity_id)
        domain = entity_id.split(".", 1)[0]
        service = "turn_on" if enabled else "turn_off"
        # The service call itself is best-effort: HA may return 500 for slow
        # BLE/cloud devices that still execute the command shortly after.
        try:
            self._request(
                "POST",
                "/api/services/{}/{}".format(domain, service),
                {"entity_id": entity_id},
            )
        except MiHomeError:
            pass  # fall through: state confirmation below decides success
        expected = "on" if enabled else "off"
        deadline = time.monotonic() + _CONFIRM_WINDOW_S
        while True:
            try:
                state = self.get_state(entity_id)
            except MiHomeError:
                state = None  # transient read failure: keep polling
            if state and state.get("state") == expected:
                break
            if time.monotonic() >= deadline:
                raise MiHomeError(
                    "MiHome state did not confirm requested power transition"
                )
            time.sleep(_CONFIRM_INTERVAL_S)
        return {"ok": True, "entity_id": entity_id, "state": expected}

    def notify_text(self, entity_id: str, text: str) -> dict:
        """Send text to a notify.* entity (e.g. 小爱音箱 播放文本) via HA.

        Independent of the light/switch allowlist: the target entity comes
        from the gateway's own HOMEMIND_SPEAK_ENTITY configuration.
        """
        if not self.enabled:
            raise MiHomeError(
                "MiHome adapter disabled; set HOMEMIND_HA_URL and HOMEMIND_HA_TOKEN"
            )
        if not isinstance(entity_id, str) or not entity_id.startswith("notify."):
            raise MiHomeError("speak entity must be a notify.* entity")
        text = (text or "").strip()
        if not text:
            raise MiHomeError("speak text is empty")
        if len(text) > 500:
            text = text[:500]
        self._request(
            "POST",
            "/api/services/notify/send_message",
            {"entity_id": entity_id, "message": text},
        )
        return {"ok": True, "entity_id": entity_id, "len": len(text)}

    def execute(self, action: str, params: Optional[dict] = None) -> dict:
        if action not in _POWER_ACTIONS:
            raise MiHomeError("MiHome action is not allowed")
        params = params or {}
        entity_id = params.get("entity_id")
        if action == "mihome.get_state":
            return self.get_state(entity_id)
        enabled = params.get("on")
        if not isinstance(enabled, bool):
            raise MiHomeError("mihome.set_power requires boolean params.on")
        return self.set_power(entity_id, enabled)

    @staticmethod
    def _clean_name(raw: str, domain: str) -> str:
        """小米集成的 friendly_name 常是"设备名 属性名 属性名"（如"阳台开关 开关 开关"）。

        去掉相邻重复 token 与与实体域重复的尾部领域词，得到"阳台开关"。
        """
        tokens = raw.split()
        dedup = []
        for token in tokens:
            if not dedup or dedup[-1] != token:
                dedup.append(token)
        domain_words = {"switch": ("开关", "插座"), "light": ("灯", "灯光")}
        if len(dedup) >= 2 and dedup[-1] in domain_words.get(domain, ()):
            dedup = dedup[:-1]
        return " ".join(dedup) or raw.strip()

    def list_entities(self) -> list:
        """[{entity_id, name, state}] for allowlisted entities; [] when disabled.

        Names come from Home Assistant friendly_name so clients can render
        human-readable labels (e.g. 阳台开关) instead of raw entity ids.
        """
        if not self.enabled:
            return []
        out = []
        for entity_id in sorted(self.allowed_entities):
            domain = entity_id.split(".", 1)[0]
            try:
                state = self.get_state(entity_id)
            except MiHomeError:
                out.append({"entity_id": entity_id, "name": entity_id, "state": "unknown"})
                continue
            raw_name = (state.get("attributes") or {}).get("friendly_name") or entity_id
            out.append({
                "entity_id": entity_id,
                "name": self._clean_name(str(raw_name), domain),
                "state": state.get("state"),
            })
        return out


__all__ = ["HomeAssistantMiHomeAdapter", "MiHomeError"]
