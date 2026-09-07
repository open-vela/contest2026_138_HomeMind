#!/usr/bin/env python3
"""Static validation for the HomeMind runtime Skill.

This check deliberately does not claim device execution. It verifies that the
checked-in Markdown is compatible with the current flat-file ai_agent loader,
stays within the device installer buffer, and documents the intended whitelist.
"""

from __future__ import annotations

import argparse
import re
from pathlib import Path


MAX_DEVICE_SKILL_BYTES = 8192
REQUIRED = (
    "get_current_time",
    "cron_add",
    "led_control",
    '"action_args": "{\\"action\\":\\"toggle\\"}"',
    "at_epoch",
    "delete_after_run",
)
FORBIDDEN = (
    "api_key",
    "Authorization:",
    "set_wifi",
)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "path",
        nargs="?",
        default="app/homemind/skills/home_security.md",
    )
    args = parser.parse_args()
    path = Path(args.path)
    errors: list[str] = []

    if path.suffix.lower() != ".md":
        errors.append("file must use the flat .md runtime format")
    if not path.is_file():
        errors.append(f"file not found: {path}")
        print("SKILL-INVALID")
        for error in errors:
            print(f"- {error}")
        return 1

    raw = path.read_bytes()
    text = raw.decode("utf-8", "strict")
    lines = text.splitlines()
    if not lines or not re.fullmatch(r"# .+", lines[0]):
        errors.append("first line must be '# title'")
    if len(raw) >= MAX_DEVICE_SKILL_BYTES:
        errors.append(
            f"file is {len(raw)} bytes; installer limit is below {MAX_DEVICE_SKILL_BYTES}"
        )

    for needle in REQUIRED:
        if needle not in text:
            errors.append(f"missing required contract: {needle}")
    for needle in FORBIDDEN:
        if needle in text:
            errors.append(f"forbidden capability or secret marker present: {needle}")

    if "15 秒" not in text or "单次" not in text:
        errors.append("active demo must be explicitly single-run and 15 seconds")
    if "静态文件存在不等于设备已加载" not in text:
        errors.append("must distinguish static validation from device evidence")

    if errors:
        print("SKILL-INVALID")
        for error in errors:
            print(f"- {error}")
        return 1

    print("SKILL-VALID")
    print(f"path={path}")
    print(f"bytes={len(raw)}")
    print("loader=flat-md")
    print("tools=get_current_time,cron_add,led_control,get_device_info")
    print("active_demo=single-run-after-15-seconds")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
