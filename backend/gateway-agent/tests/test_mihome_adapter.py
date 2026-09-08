import json
import pathlib
import sys
from urllib.parse import urlparse

sys.path.insert(0, str(pathlib.Path(__file__).parents[1]))

from mihome_adapter import HomeAssistantMiHomeAdapter, MiHomeError


class FakeResponse:
    def __init__(self, payload):
        self.payload = json.dumps(payload).encode("utf-8")

    def read(self):
        return self.payload


def test_set_power_uses_allowlist_and_reads_back_state():
    calls = []
    state = {"light.desk": "off"}

    def opener(request, timeout):
        calls.append((request.get_method(), urlparse(request.full_url).path))
        body = json.loads(request.data.decode("utf-8")) if request.data else None
        if request.get_method() == "POST":
            state[body["entity_id"]] = "on"
            return FakeResponse([])
        return FakeResponse({
            "entity_id": "light.desk",
            "state": state["light.desk"],
            "attributes": {"friendly_name": "Desk"},
        })

    adapter = HomeAssistantMiHomeAdapter(
        "http://ha.local",
        "test-token",
        ["light.desk"],
        opener=opener,
    )
    assert adapter.execute(
        "mihome.set_power", {"entity_id": "light.desk", "on": True}
    ) == {"ok": True, "entity_id": "light.desk", "state": "on"}
    assert calls == [
        ("POST", "/api/services/light/turn_on"),
        ("GET", "/api/states/light.desk"),
    ]


def test_entity_outside_allowlist_fails_closed():
    adapter = HomeAssistantMiHomeAdapter(
        "http://ha.local", "test-token", ["light.desk"], opener=lambda *_a, **_k: None
    )
    try:
        adapter.execute(
            "mihome.set_power", {"entity_id": "switch.kitchen", "on": True}
        )
    except MiHomeError as exc:
        assert "allowlist" in str(exc)
    else:
        raise AssertionError("unallowlisted entity must be rejected")


def test_set_power_polls_until_state_confirms():
    reads = {"n": 0}

    def opener(request, timeout):
        if request.get_method() == "POST":
            return FakeResponse([])
        reads["n"] += 1
        # Entity state lags behind the service call for two reads.
        state = "on" if reads["n"] < 3 else "off"
        return FakeResponse({
            "entity_id": "light.desk",
            "state": state,
            "attributes": {},
        })

    adapter = HomeAssistantMiHomeAdapter(
        "http://ha.local", "test-token", ["light.desk"], opener=opener
    )
    assert adapter.execute(
        "mihome.set_power", {"entity_id": "light.desk", "on": False}
    ) == {"ok": True, "entity_id": "light.desk", "state": "off"}
    assert reads["n"] == 3


def test_missing_configuration_is_disabled_without_network_call():
    called = []

    adapter = HomeAssistantMiHomeAdapter(opener=lambda *_a, **_k: called.append(True))
    assert adapter.describe() == {"enabled": False, "allowed_entities": 0}
    try:
        adapter.execute(
            "mihome.set_power", {"entity_id": "light.desk", "on": True}
        )
    except MiHomeError:
        pass
    else:
        raise AssertionError("missing configuration must fail closed")
    assert called == []



def test_set_power_survives_service_error_and_confirms_via_state():
    """HA may answer 500 (slow BLE device) yet the command applies: the
    adapter must fall back to state polling instead of failing immediately."""
    reads = {"n": 0}
    state = {"switch.balcony": "off"}

    def opener(request, timeout):
        if request.get_method() == "POST":
            raise OSError("simulated HA 500: 设备操作超时")
        reads["n"] += 1
        if reads["n"] >= 2:
            state["switch.balcony"] = "on"  # device executes late
        return FakeResponse({
            "entity_id": "switch.balcony",
            "state": state["switch.balcony"],
            "attributes": {},
        })

    adapter = HomeAssistantMiHomeAdapter(
        "http://ha.local", "test-token", ["switch.balcony"], opener=opener
    )
    assert adapter.execute(
        "mihome.set_power", {"entity_id": "switch.balcony", "on": True}
    ) == {"ok": True, "entity_id": "switch.balcony", "state": "on"}


def test_set_power_fails_when_state_never_confirms(monkeypatch):
    import pytest

    import mihome_adapter as mod

    monkeypatch.setattr(mod, "_CONFIRM_WINDOW_S", 0.3)
    monkeypatch.setattr(mod, "_CONFIRM_INTERVAL_S", 0.05)

    def opener(request, timeout):
        if request.get_method() == "POST":
            raise OSError("simulated HA 500")
        return FakeResponse({
            "entity_id": "switch.balcony",
            "state": "off",  # device never applies the command
            "attributes": {},
        })

    adapter = HomeAssistantMiHomeAdapter(
        "http://ha.local", "test-token", ["switch.balcony"], opener=opener
    )
    with pytest.raises(MiHomeError):
        adapter.execute(
            "mihome.set_power", {"entity_id": "switch.balcony", "on": True}
        )
