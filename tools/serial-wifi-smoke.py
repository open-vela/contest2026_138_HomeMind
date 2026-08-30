#!/usr/bin/env python3
"""Run the first Wi-Fi smoke test without reopening the USB serial port."""

import argparse
from pathlib import Path
import re
import time

import serial


def read_for(port: serial.Serial, seconds: float) -> bytes:
    deadline = time.monotonic() + seconds
    data = bytearray()
    while time.monotonic() < deadline:
        data.extend(port.read(4096))
    return bytes(data)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="/dev/ttyACM0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--command-file", required=True)
    args = parser.parse_args()

    command_path = Path(args.command_file)
    wifi_command = command_path.read_bytes().strip() + b"\r\n"
    command_path.unlink()

    output = bytearray()
    with serial.Serial(args.port, args.baud, timeout=0.25) as port:
        output.extend(read_for(port, 2))
        port.write(b"\r\n")
        port.flush()
        output.extend(read_for(port, 2))

        port.write(b"ai_agent\r\n")
        port.flush()
        output.extend(read_for(port, 5))

        port.write(wifi_command)
        port.flush()
        output.extend(read_for(port, 105))

        port.write(b"net_status\r\n")
        port.flush()
        output.extend(read_for(port, 6))

    text = output.decode("utf-8", "replace")
    text = re.sub(
        r"set_wifi\s+\S+\s+\S+",
        "set_wifi <redacted-ssid> <redacted-password>",
        text,
    )
    print(text, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
