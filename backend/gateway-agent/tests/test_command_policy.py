import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).parents[1]))

from command_policy import CommandPolicy


def test_valid_command_is_claimed_once_and_status_is_replayed():
    now = [1000.0]
    policy = CommandPolicy(["led.on"], clock=lambda: now[0])
    payload = {"command_id": "abc123", "action": "led.on", "ttl": 30, "ts": 995}

    first = policy.claim(payload)
    assert first.accepted and first.status == "queued"
    policy.update("abc123", "done")
    duplicate = policy.claim(payload)
    assert not duplicate.accepted
    assert duplicate.reason == "duplicate"
    assert duplicate.status == "done"


def test_expired_and_future_commands_are_rejected():
    policy = CommandPolicy(["led.on"], clock=lambda: 1000.0)
    assert policy.claim({"command_id": "old", "action": "led.on", "ttl": 10, "ts": 989}).reason == "command_expired"
    assert policy.claim({"command_id": "new", "action": "led.on", "ttl": 10, "ts": 1010}).reason == "timestamp_in_future"


def test_unsafe_actions_and_malformed_envelopes_fail_closed():
    policy = CommandPolicy(["led.on"], clock=lambda: 1000.0)
    assert policy.claim({"command_id": "bad-action", "action": "run_shell", "ttl": 10, "ts": 1000}).reason == "action_not_allowed"
    assert policy.claim({"command_id": "bad ttl", "action": "led.on", "ttl": 10, "ts": 1000}).reason == "invalid_command_id"
    assert policy.claim({"command_id": "bad-ts", "action": "led.on", "ttl": 10}).reason == "invalid_timestamp"
