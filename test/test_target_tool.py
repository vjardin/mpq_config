# SPDX-License-Identifier: BSD-4-Clause
# Copyright 2026 Vincent Jardin, Free Mobile <vjardin@free.fr>
"""On-target end-to-end tests for the `mpq_config` binary running on
the board. Covers every subcommand against the real chip, plus
chip-health and MPS GUI round-trip including upload of factory.txt."""

import base64
import re
import pytest

pytestmark = pytest.mark.target


EXPECTED_LIVE_ONLY = {
    0x22, 0x40, 0x42, 0x43, 0x44, 0x58, 0x59,
    0x79, 0x88, 0x8B, 0x8C, 0x8D, 0x98, 0x9B, 0xFB,
}
REQUIRED_DRIFT_REGS = {"C2", "D2"}
EXPECTED_DRIFT_REGS = {"C2", "D2", "F8"}


@pytest.fixture(scope="module", autouse=True)
def quiesce(board):
    board.run("echo 0 > /sys/kernel/tracing/events/mpq8785/enable 2>/dev/null",
              t=3)
    board.run("echo 0 > /sys/kernel/debug/mpq8785/0-0010/alarm_poll_interval_ms",
              t=3)


@pytest.fixture(scope="module")
def chip_dump(board, chip_bus, chip_addr):
    """A fresh on-board read --all dump. Cached for the module."""
    rc, body = board.run(
        f"mpq_config read --bus {chip_bus} --addr 0x{chip_addr:02x} "
        f"--all --output /tmp/chip.dmp 2>&1", t=15)
    assert rc == 0, f"read failed: {body}"
    rc, dump = board.run("cat /tmp/chip.dmp", t=5)
    return dump


def parse_dump(text):
    out = {}
    for line in text.splitlines():
        if line.startswith("#") or not line.strip():
            continue
        m = re.match(r"^\s*0x([0-9A-Fa-f]+)\s+(\d+)\s+0x([0-9A-Fa-f]+)", line)
        if m:
            out[int(m.group(1), 16)] = (int(m.group(2)),
                                        int(m.group(3), 16))
    return out


# Tool surface
def test_help_lists_all_subcommands(board):
    rc, body = board.run("mpq_config help 2>&1", t=5)
    for sub in ("read", "write", "to-csv", "from-csv", "to-mps",
                "from-mps", "diff", "explain", "live-diff"):
        assert sub in body, f"on-board help missing `{sub}`"


def test_read_dumps_full_inventory(chip_dump):
    parsed = parse_dump(chip_dump)
    assert len(parsed) >= 85, f"expected >=85 entries, got {len(parsed)}"


def test_csv_roundtrip_on_chip(board):
    rc, _ = board.run(
        "mpq_config to-csv /tmp/chip.dmp /tmp/chip.csv && "
        "mpq_config from-csv /tmp/chip.csv /tmp/chip.rt.dmp && "
        "mpq_config diff /tmp/chip.dmp /tmp/chip.rt.dmp 2>&1 | tail -1", t=10)
    assert rc == 0, "CSV round-trip diff was non-zero"


def test_mps_emit_envelope_on_chip(board):
    rc, _ = board.run("mpq_config to-mps /tmp/chip.dmp /tmp/chip.mps.txt 2>&1",
                      t=10)
    assert rc == 0
    rc, body = board.run(
        "grep -cE 'WRITE_PROTECTION|^Product ID:|^I2C Address:|"
        "^4-digit Code:|^END$|^CRC_CHECK_START$|^CRC_CHECK_STOP$' "
        "/tmp/chip.mps.txt", t=5)
    m = re.search(r"\d+", body)
    assert m and int(m.group()) >= 7


def test_mps_roundtrip_on_chip(board):
    rc, _ = board.run(
        "mpq_config from-mps /tmp/chip.mps.txt /tmp/chip.mps.rt.dmp", t=10)
    assert rc == 0
    rc, body = board.run(
        "mpq_config diff /tmp/chip.dmp /tmp/chip.mps.rt.dmp 2>&1", t=10)
    # Only telemetry should appear as "only in chip.dmp".
    only_orig = re.findall(r"\s+0x([0-9A-F]+).*only in.*chip\.dmp", body)
    non_telem = [int(r, 16) for r in only_orig
                 if int(r, 16) not in EXPECTED_LIVE_ONLY]
    assert non_telem == [], f"on-board MPS RT dropped: {non_telem}"


def test_explain_decodes_vout_mode_linear_exp(board):
    rc, body = board.run(
        "mpq_config explain /tmp/chip.dmp 2>&1 | grep -E '^  0x20 '", t=5)
    assert "exp=-9" in body, f"explain VOUT_MODE: {body!r}"


def test_self_diff_clean(board):
    rc, body = board.run("mpq_config diff /tmp/chip.dmp /tmp/chip.dmp 2>&1 | tail -1",
                         t=10)
    assert "0 differences" in body


def test_live_diff_vs_same_instant_dump_has_only_telemetry_jitter(board, chip_bus, chip_addr):
    """`live-diff` should produce at most a few LSB drifts on
    telemetry (chip workload varies between two reads). The fixed
    `live-diff` reads exactly the saved register set, so there
    should be NO `only in saved` entries (only value drifts)."""
    rc, body = board.run(
        f"mpq_config live-diff --bus {chip_bus} --addr 0x{chip_addr:02x} "
        f"--input /tmp/chip.dmp 2>&1", t=15)
    only_in = re.findall(r"only in", body)
    assert only_in == [], f"live-diff should have no 'only in' regs: {body}"
    m = re.search(r"(\d+) differences", body)
    assert m and int(m.group(1)) <= 4, f"too many diffs: {m.group(1) if m else '?'}"


def test_write_refuses_with_alarm_poll_active(board, chip_bus, chip_addr):
    board.run("echo 5000 > /sys/kernel/debug/mpq8785/0-0010/alarm_poll_interval_ms",
              t=3)
    try:
        rc, body = board.run(
            f"mpq_config write --bus {chip_bus} --addr 0x{chip_addr:02x} "
            f"--input /tmp/chip.dmp 2>&1 | head -10", t=10)
        assert "REFUSING" in body, f"write didn't refuse: {body!r}"
    finally:
        board.run("echo 0 > /sys/kernel/debug/mpq8785/0-0010/alarm_poll_interval_ms",
                  t=3)


def test_empty_file_load_refuses(board):
    board.run("printf '# header only\\n' > /tmp/empty.dmp", t=3)
    rc, body = board.run("mpq_config diff /tmp/empty.dmp /tmp/chip.dmp 2>&1",
                         t=5)
    assert rc != 0
    assert "parsed 0 entries" in body


# Factory MPS round-trip (pushes the .txt onto the board)
@pytest.fixture(scope="module")
def factory_pushed(board, fixtures):
    """Push factory.txt to /tmp/factory.txt on the board via base64
    chunks. Returns the path."""
    src = open(fixtures["factory_txt"], "rb").read()
    b64 = base64.b64encode(src).decode()
    # 200-char chunks: leave room for the `printf '%s' '...' >> file;
    # echo TAG$?\r` envelope and the busybox terminal line buffer at
    # 115200 baud + canonical mode (~4 KiB but with margin for safety).
    chunk = 200
    board.run("rm -f /tmp/factory.txt /tmp/factory.txt.b64", t=3)
    for i in range(0, len(b64), chunk):
        board.run(f"printf '%s' '{b64[i:i+chunk]}' >> /tmp/factory.txt.b64",
                  t=5)
    board.run("base64 -d /tmp/factory.txt.b64 > /tmp/factory.txt", t=5)
    rc, body = board.run("wc -c /tmp/factory.txt", t=3)
    # Anchor on `<size> /tmp/factory.txt` so a wrapped sentinel-tag
    # echo can't trick re.search into matching its own hex digits.
    m = re.search(r"(\d+)\s+/tmp/factory\.txt", body)
    pushed = int(m.group(1)) if m else 0
    assert pushed == len(src), f"pushed {pushed} of {len(src)} bytes; body={body!r}"
    return "/tmp/factory.txt"


def test_factory_txt_pushable(factory_pushed):
    # The push fixture asserts the byte count; reaching here means OK.
    assert factory_pushed == "/tmp/factory.txt"


def test_live_diff_vs_factory_txt(board, factory_pushed, chip_bus, chip_addr):
    """live-diff against the .txt file directly (auto-detect MPS).
    Expect exactly the real drifts from the factory comparison --
    2 required (C2, D2) and optionally CRC_USER."""
    rc, body = board.run(
        f"mpq_config live-diff --bus {chip_bus} --addr 0x{chip_addr:02x} "
        f"--input /tmp/factory.txt 2>&1", t=20)
    drifts = re.findall(
        r"^\s*0x([0-9A-F]+) (\S+)\s+0x[0-9A-F]+ -> 0x[0-9A-F]+",
        body, re.MULTILINE)
    got = set(r for r, _ in drifts)
    unexpected = got - EXPECTED_DRIFT_REGS
    assert unexpected == set(), f"unexpected drifts: {unexpected}"
    missing = REQUIRED_DRIFT_REGS - got
    assert missing == set(), f"required drifts missing: {missing}"
