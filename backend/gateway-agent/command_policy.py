"""Validation and in-process idempotency for gateway MQTT commands."""

from collections import OrderedDict
from dataclasses import dataclass
import math
import re
import threading
import time
from typing import Callable, Iterable, Optional


_COMMAND_ID_RE = re.compile(r"^[A-Za-z0-9_-]{1,64}$")


@dataclass(frozen=True)
class Decision:
    accepted: bool
    reason: str
    status: Optional[str] = None


@dataclass
class _Record:
    status: str
    updated_at: float


class CommandPolicy:
    """Reject stale/unsafe commands and suppress MQTT QoS1 redeliveries."""

    def __init__(
        self,
        allowed_actions: Iterable[str],
        max_ttl: int = 300,
        future_skew: int = 5,
        max_records: int = 256,
        clock: Callable[[], float] = time.time,
    ):
        self.allowed_actions = frozenset(allowed_actions)
        self.max_ttl = max_ttl
        self.future_skew = future_skew
        self.max_records = max_records
        self.clock = clock
        self._records = OrderedDict()
        self._lock = threading.Lock()

    def _prune(self, now: float) -> None:
        while len(self._records) > self.max_records:
            self._records.popitem(last=False)
        expiry = max(self.max_ttl * 2, 60)
        for cid, record in list(self._records.items()):
            if now - record.updated_at > expiry:
                del self._records[cid]

    def claim(self, payload: object) -> Decision:
        with self._lock:
            now = self.clock()
            self._prune(now)
            if not isinstance(payload, dict):
                return Decision(False, "payload_not_object")

            cid = payload.get("command_id")
            if not isinstance(cid, str) or not _COMMAND_ID_RE.fullmatch(cid):
                return Decision(False, "invalid_command_id")
            previous = self._records.get(cid)
            if previous is not None:
                return Decision(False, "duplicate", previous.status)

            action = payload.get("action")
            if not isinstance(action, str) or action not in self.allowed_actions:
                return Decision(False, "action_not_allowed")

            ttl = payload.get("ttl")
            ts = payload.get("ts")
            if isinstance(ttl, bool) or not isinstance(ttl, int):
                return Decision(False, "invalid_ttl")
            if ttl < 1 or ttl > self.max_ttl:
                return Decision(False, "ttl_out_of_range")
            if isinstance(ts, bool) or not isinstance(ts, (int, float)):
                return Decision(False, "invalid_timestamp")
            if not math.isfinite(float(ts)):
                return Decision(False, "invalid_timestamp")
            age = now - float(ts)
            if age < -self.future_skew:
                return Decision(False, "timestamp_in_future")
            if age > ttl:
                return Decision(False, "command_expired")

            self._records[cid] = _Record("queued", now)
            return Decision(True, "accepted", "queued")

    def update(self, command_id: str, status: str) -> None:
        with self._lock:
            record = self._records.get(command_id)
            if record is not None:
                record.status = status
                record.updated_at = self.clock()
                self._records.move_to_end(command_id)
