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
# new state; poll briefly instead of failing on the first read.
_CONFIRM_WINDOW_S = 3.0
_CONFIRM_INTERVAL_S = 0.4


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
        self._request(
            "POST",
            "/api/services/{}/{}".format(domain, service),
            {"entity_id": entity_id},
        )
        expected = "on" if enabled else "off"
        deadline = time.monotonic() + _CONFIRM_WINDOW_S
        while True:
            state = self.get_state(entity_id)
            if state.get("state") == expected:
                break
            if time.monotonic() >= deadline:
                raise MiHomeError(
                    "MiHome state did not confirm requested power transition"
                )
            time.sleep(_CONFIRM_INTERVAL_S)
        return {"ok": True, "entity_id": entity_id, "state": expected}

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


__all__ = ["HomeAssistantMiHomeAdapter", "MiHomeError"]
