#!/usr/bin/env python3
"""Run a one-shot serial command sequence, supporting @sleep directives."""

import argparse
from pathlib import Path
import re
import time

import serial


PROMPT_RE = re.compile(rb"(?:^|[\r\n])(?:nsh|vela)>[ \t]*(?:\r|\n|$)")


def read_for(port: serial.Serial, seconds: float) -> bytes:
    deadline = time.monotonic() + seconds
    data = bytearray()
    while time.monotonic() < deadline:
        data.extend(port.read(4096))
    return bytes(data)


def read_until_prompt(port: serial.Serial, timeout: float) -> bytes:
    """Read one command's result without queueing the next command early."""
    deadline = time.monotonic() + timeout
    data = bytearray()
    while time.monotonic() < deadline:
        chunk = port.read(4096)
        if chunk:
            data.extend(chunk)
            # The terminal echoes the submitted command as `vela> command`.
            # Only accept a prompt that ends a line, otherwise a long-running
            # command such as net_test can be reported as complete early.
            clean = re.sub(rb"\x1b\[[0-?]*[ -/]*[@-~]", b"", bytes(data))
            if PROMPT_RE.search(clean):
                return bytes(data)
    return bytes(data)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="/dev/ttyACM0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--command-file", required=True)
    parser.add_argument(
        "--command-timeout",
        type=float,
        default=45.0,
        help="maximum time to wait for nsh> or vela> after each command",
    )
    parser.add_argument(
        "--fixed-delay",
        type=float,
        default=0.0,
        help="after each command, read for this many seconds instead of syncing on a prompt",
    )
    args = parser.parse_args()

    command_path = Path(args.command_file)
    commands = command_path.read_text(encoding="utf-8").splitlines()
    command_path.unlink()

    output = bytearray()
    with serial.Serial(args.port, args.baud, timeout=0.25) as port:
        output.extend(read_for(port, 2))
        port.write(b"\r\n")
        port.flush()
        output.extend(read_for(port, 2))
        # The blank line above can leave a fresh `vela>` prompt queued after
        # the initial read window.  Discard that prompt before the first
        # command so read_until_prompt cannot advance one command early.
        port.reset_input_buffer()

        for command in commands:
            command = command.strip()
            if not command or command.startswith("#"):
                continue
            if command.startswith("@sleep "):
                output.extend(read_for(port, float(command.split()[1])))
                continue
            port.write(command.encode("utf-8") + b"\r\n")
            port.flush()
            if args.fixed_delay > 0:
                output.extend(read_for(port, args.fixed_delay))
            else:
                output.extend(read_until_prompt(port, args.command_timeout))

        if args.fixed_delay > 0:
            output.extend(read_for(port, 1))

    text = output.decode("utf-8", "replace")
    text = re.sub(r"(wapi\s+psk\s+\S+\s+)\S+", r"\1<redacted>", text)
    text = re.sub(r"(set_wifi\s+\S+\s+)\S+", r"\1<redacted>", text)
    text = re.sub(r'("psk"\s*:\s*")[^"]*', r'\1<redacted>', text)
    print(text, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
