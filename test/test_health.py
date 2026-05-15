# SPDX-License-Identifier: BSD-4-Clause
# Copyright 2026 Vincent Jardin, Free Mobile <vjardin@free.fr>
"""Chip-health envelope checks driven from a register-value table.

The `reg` fixture is parametrized so each test runs twice:

  * `fixture`  -- reads the saved u2200-live.dmp from test-data/.
                  Always runs (host marker). No hardware needed.
  * `target`   -- reads the live chip via mpq_config on the board.
                  Skipped without MPQ_SERIAL_PORT.

Same assertions against both sources. It guarantees the saved fixture
stays in sync with what a healthy chip looks like, and that the
on-target read path produces values our envelope assertions accept."""

import re
import pytest


HEALTHY_VIN_MV = (11000, 13200)
HEALTHY_VOUT_MV = (780, 920)
HEALTHY_IOUT_MA = (0, 40000)
HEALTHY_TEMP_C = (-10, 105)

STATUS_WORD_FATAL_BITS = {
    15: "VOUT",
    14: "IOUT_POUT",
    13: "INPUT",
    11: "POWER_NOT_GOOD",
    7:  "BUSY",
    4:  "CML",
    3:  "VOUT_OV",
    2:  "IOUT_OC",
    1:  "VIN_UV",
}


def _parse_dump(text):
    out = {}
    for line in text.splitlines():
        if line.startswith("#") or not line.strip():
            continue
        m = re.match(r"^\s*0x([0-9A-Fa-f]+)\s+(\d+)\s+0x([0-9A-Fa-f]+)", line)
        if m:
            out[int(m.group(1), 16)] = int(m.group(3), 16)
    return out


@pytest.fixture(scope="module", params=[
    pytest.param("fixture", marks=pytest.mark.host),
    pytest.param("target",  marks=pytest.mark.target),
])
def reg(request, fixtures, chip_bus, chip_addr):
    """Return a function `reg(addr) -> value | None` over a snapshot
    of the chip's register state. Source picked per parametrize id."""
    if request.param == "fixture":
        # Host: read the saved live dump from test-data/.
        with open(fixtures["live_dmp"]) as f:
            table = _parse_dump(f.read())
    else:
        # Target: read the live chip via mpq_config on the board.
        board = request.getfixturevalue("board")
        board.run("echo 0 > /sys/kernel/tracing/events/mpq8785/enable 2>/dev/null", t=3)
        board.run("echo 0 > /sys/kernel/debug/mpq8785/0-0010/alarm_poll_interval_ms", t=3)
        rc, body = board.run(
            f"mpq_config read --bus {chip_bus} --addr 0x{chip_addr:02x} "
            f"--all --output /tmp/health.dmp 2>&1", t=15)
        assert rc == 0, body
        rc, body = board.run("cat /tmp/health.dmp", t=5)
        table = _parse_dump(body)

    def get(r):
        return table.get(r)
    return get


def test_status_word_no_fatal_bits(reg):
    v = reg(0x79) or 0
    fatal = [n for b, n in STATUS_WORD_FATAL_BITS.items() if v & (1 << b)]
    assert not fatal, f"STATUS_WORD=0x{v:04X} has fatal bits {fatal}"


def test_vin_in_healthy_envelope(reg):
    raw = reg(0x88)
    assert raw is not None
    mv = raw * 25
    assert HEALTHY_VIN_MV[0] <= mv <= HEALTHY_VIN_MV[1], \
        f"VIN raw=0x{raw:04X} = {mv} mV"


def test_vout_in_healthy_envelope(reg):
    raw = reg(0x8B)
    assert raw is not None
    mv = round(raw * 1000 / 512)
    assert HEALTHY_VOUT_MV[0] <= mv <= HEALTHY_VOUT_MV[1], \
        f"VOUT raw=0x{raw:04X} = {mv} mV"


def test_iout_in_healthy_envelope(reg):
    raw = reg(0x8C)
    assert raw is not None
    ma = round(raw * 125 / 2)
    assert HEALTHY_IOUT_MA[0] <= ma <= HEALTHY_IOUT_MA[1], \
        f"IOUT raw=0x{raw:04X} = {ma} mA"


def test_temperature_in_healthy_envelope(reg):
    raw = reg(0x8D)
    assert raw is not None
    c = raw if raw < 0x8000 else raw - 0x10000
    assert HEALTHY_TEMP_C[0] <= c <= HEALTHY_TEMP_C[1], \
        f"TEMP raw=0x{raw:04X} = {c} C"


def test_vout_mode_linear_exp_minus_9(reg):
    """MPQ8646 personality on the target board ships VOUT_MODE = 0x17
    (LINEAR16 exp = -9, 1 LSB = 1/512 V). Departure means the chip
    is using a different scaling and downstream decoders drift --
    see the board PMBus documentation."""
    v = reg(0x20)
    assert v == 0x17, f"VOUT_MODE=0x{v:02X}, expected 0x17"


def test_vout_scale_loop_no_external_divider(reg):
    """0x0400 (1024) means no external feedback divider -- the rail
    is remote-sensed across the SoC core balls."""
    v = reg(0x29)
    assert v == 0x0400, f"VOUT_SCALE_LOOP=0x{v:04X}, expected 0x0400"


def test_mfr_addr_pmbus_in_1x_window(reg):
    """MPQ8646 personality forces MFR_ADDR_PMBUS bits[6:4] = 0b001
    (selects 0x1X window; chip lands at 0x10)."""
    v = reg(0xD2)
    assert v is not None
    window = (v >> 4) & 0x7
    assert window == 0b001, \
        f"MFR_ADDR_PMBUS raw=0x{v:02X}, window=0b{window:03b}"


def test_pmbus_revision_1_3(reg):
    v = reg(0x98)
    assert v == 0x33, f"PMBUS_REVISION=0x{v:02X}, expected 0x33"
