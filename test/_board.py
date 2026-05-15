# SPDX-License-Identifier: BSD-4-Clause
# Copyright 2026 Vincent Jardin, Free Mobile <vjardin@free.fr>
"""Serial-console shell driver for the on-target tests.

Robust against shell terminal line-wrap at column 80 (uses both
the literal-tag echo and the post-eval sentinel to bracket output)
and against kernel printk lines that concatenate onto cat stdout
without a leading newline (non-anchored noise strip)."""

import re
import time

try:
    import serial
except ImportError:
    raise SystemExit("pip install pyserial -- required for on-target tests")


# Strip kernel printk noise (timestamps + mpq8785 tracepoints) that
# the on-board mpq8785 driver emits via the kernel ring buffer when
# the i2c xfers fire. These bytes interleave with cat output on the
# same UART without a leading newline boundary.
_NOISE_RE = re.compile(
    r'\[\s*\d+\.\d+\][^\n]*'        # `[  123.456789] anything`
    r'|mpq8785-trace:[^\n]*'        # bare tracepoint continuation
)


class Board:
    def __init__(self, port: str, baud: int, timeout: float = 0.2):
        self.s = serial.Serial(port, baud, timeout=timeout)
        # Wake the shell and drain any in-flight bytes.
        self.s.write(b"\r")
        time.sleep(0.3)
        self.s.reset_input_buffer()

    def close(self):
        try:
            self.s.close()
        except Exception:
            pass

    def run(self, cmd: str, t: float = 30.0) -> tuple[int, str]:
        """Send `cmd`, wait for tag-bracketed sentinel, return
        (rc, body). Body has terminal echo, ANSI, CR, and kernel
        noise stripped. `t` is the per-command timeout in seconds."""
        tag = f"__E{int(time.monotonic_ns()):x}__"
        self.s.write(f"{cmd}; echo {tag}$?\r".encode())
        buf = b""
        deadline = time.monotonic() + t
        sentinel = re.compile(re.escape(tag).encode() + rb'\d')
        while time.monotonic() < deadline:
            chunk = self.s.read(4096)
            if chunk:
                buf += chunk
                if sentinel.search(buf):
                    # Let trailing kernel printk noise settle.
                    time.sleep(0.05)
                    buf += self.s.read(4096)
                    break
        text = buf.decode("utf-8", errors="replace")
        text = re.sub(r'\x1b\[[0-9;]*[a-zA-Z]', '', text).replace('\r', '')
        text = _NOISE_RE.sub('', text)

        # Body starts after the literal-tag-echo line; ends at the
        # actual post-eval sentinel `{tag}<digit>`.
        echo_marker = f"echo {tag}$?"
        p = text.find(echo_marker)
        if p >= 0:
            nl = text.find('\n', p + len(echo_marker))
            body_start = nl + 1 if nl >= 0 else 0
        else:
            body_start = 0
        m = re.search(re.escape(tag) + r'(\d+)', text[body_start:])
        if m:
            body_end = m.start() + body_start
            rc = int(m.group(1))
        else:
            body_end = len(text)
            rc = -1
        return rc, text[body_start:body_end].strip()

    def cat(self, path: str, t: float = 5.0) -> str:
        """Read a file's contents, returning the stripped body."""
        rc, body = self.run(f"cat {path}", t=t)
        return body if rc == 0 else ""
