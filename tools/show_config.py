#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-4-Clause
# Copyright 2026 Vincent Jardin, Free Mobile <vjardin@free.fr>
"""Open /dev/ttyUSB0 @ 115200, run mpq_config on the board, print
its decoded config to stdout.

Coexists with the kernel mpq8785 driver (mpq_config uses
I2C_SLAVE_FORCE; bus serialization is via the adapter mutex)."""

import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pip install pyserial")


PORT = "/dev/ttyUSB0"
BAUD = 115200


def run(s, cmd: str, timeout: float = 60.0) -> str:
    tag = f"___END_{int(time.monotonic_ns()):x}___"
    s.write(f"{cmd}; echo {tag}$?\r".encode())
    buf = b""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        chunk = s.read(4096)
        if chunk:
            buf += chunk
            if tag.encode() in buf:
                break
    text = buf.decode("utf-8", errors="replace")
    i = text.find("\n")
    j = text.rfind(tag)
    return text[i + 1 : j if j > 0 else None].rstrip()


def main():
    s = serial.Serial(PORT, BAUD, timeout=0.2)
    s.write(b"\r")
    time.sleep(0.4)
    s.reset_input_buffer()

    run(s, "mpq_config read --bus 0 --addr 0x10 --all --output /tmp/u2200.dmp",
        timeout=60)
    print(run(s, "mpq_config explain /tmp/u2200.dmp", timeout=30))
    s.close()


if __name__ == "__main__":
    main()
