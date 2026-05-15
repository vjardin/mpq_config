# SPDX-License-Identifier: BSD-4-Clause
# Copyright 2026 Vincent Jardin, Free Mobile <vjardin@free.fr>
"""`explain` decoder output validation against the live fixture.

Each decoder is checked by independently re-computing the expected
value from the raw register bits (per MPQ8785 datasheet + PMBus
1.3.1 + board PMBus documentation) and comparing against what the C
decoders print."""

import re
import subprocess
import pytest

pytestmark = pytest.mark.host


def parse_dump(path):
    out = {}
    with open(path) as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            m = re.match(r"^\s*0x([0-9A-Fa-f]+)\s+(\d+)\s+0x([0-9A-Fa-f]+)", line)
            if m:
                out[int(m.group(1), 16)] = (int(m.group(2)),
                                            int(m.group(3), 16))
    return out


@pytest.fixture(scope="module")
def explain_lines(mpq_config_bin, fixtures):
    r = subprocess.run([mpq_config_bin, "explain", fixtures["live_dmp"]],
                       capture_output=True, text=True)
    assert r.returncode == 0
    lines = {}
    for line in r.stdout.splitlines():
        m = re.match(
            r"^\s*0x([0-9A-Fa-f]+)\s+0x[0-9A-Fa-f]+\s+(\S+)\s+(.*)$", line)
        if m:
            lines[int(m.group(1), 16)] = (m.group(2), m.group(3).strip())
    return lines


@pytest.fixture(scope="module")
def raw(fixtures):
    return parse_dump(fixtures["live_dmp"])


# Decoder math vs raw register values
def test_vout_mode_decoded_as_linear_exp(explain_lines):
    name, decoded = explain_lines[0x20]
    assert name == "VOUT_MODE"
    # On the target board the chip ships VOUT_MODE = 0x17 = LINEAR with exp = -9
    assert "LINEAR16" in decoded
    assert "exp=-9" in decoded


def test_vout_command_linear16_exp_minus_9(explain_lines, raw):
    """VOUT_COMMAND must follow VOUT_MODE -- not the DIRECT VID
    fallback (which was the original bug). raw / 512 V."""
    rawv = raw[0x21][1]
    expected_mv = round(rawv * 1000 / 512)
    name, decoded = explain_lines[0x21]
    m = re.search(r"(\d+) mV", decoded)
    assert m, f"VOUT_COMMAND not in mV: {decoded}"
    # 1 mV rounding tolerance
    assert abs(int(m.group(1)) - expected_mv) <= 1, \
        f"raw=0x{rawv:04X} explain={m.group(1)} expected={expected_mv}"


def test_read_vout_linear16_exp_minus_9(explain_lines, raw):
    rawv = raw[0x8B][1]
    expected_mv = round(rawv * 1000 / 512)
    _, decoded = explain_lines[0x8B]
    m = re.search(r"(\d+) mV", decoded)
    assert m
    assert abs(int(m.group(1)) - expected_mv) <= 1


def test_read_vin_25mv_per_lsb(explain_lines, raw):
    rawv = raw[0x88][1]
    _, decoded = explain_lines[0x88]
    m = re.search(r"(\d+) mV", decoded)
    assert m
    assert int(m.group(1)) == rawv * 25


def test_read_iout_62_5ma_per_lsb(explain_lines, raw):
    rawv = raw[0x8C][1]
    _, decoded = explain_lines[0x8C]
    m = re.search(r"(\d+) mA", decoded)
    assert m
    expected = rawv * 125 // 2
    assert abs(int(m.group(1)) - expected) <= 1


def test_read_temperature_signed_byte(explain_lines, raw):
    rawv = raw[0x8D][1]
    _, decoded = explain_lines[0x8D]
    m = re.search(r"(-?\d+)\s*C\b", decoded)
    assert m
    expected = rawv if rawv < 0x8000 else rawv - 0x10000
    assert int(m.group(1)) == expected


# Bitfield decoders
def test_on_off_config_bitfield(explain_lines):
    """raw=0x1E: bit4 (OPER req), bit2 (CTRL req), bit1 (AH),
    bit0=0 (soft-turn-off). Decoder spells these out."""
    _, decoded = explain_lines[0x02]
    for token in ("OPER=req", "CTRL=req-AH", "soft-turn-off"):
        assert token in decoded, f"on_off_config missing `{token}`: {decoded}"


def test_write_protect_unlocked(explain_lines):
    _, decoded = explain_lines[0x10]
    assert decoded == "unlocked"


def test_status_word_decodes_known_bits(explain_lines):
    """Live chip has STATUS_WORD = 0x1000 (NVM_SUMMARY only -- set
    after our 0xCAFE write to user NVM)."""
    _, decoded = explain_lines[0x79]
    assert "NVM_SUMMARY" in decoded


def test_protection_last_decodes_known_bits(explain_lines):
    """PROTECTION_LAST = 0x0010 (VIN_UVLO_FAULT, latched at POR)."""
    _, decoded = explain_lines[0xFB]
    assert "VIN_UVLO_FAULT" in decoded


def test_mfr_pmbus_lock_decoded(explain_lines):
    """raw = 0x0000 -> "unlocked"."""
    _, decoded = explain_lines[0xEE]
    assert decoded == "unlocked"


def test_mfr_cfg_ext_decoded(explain_lines):
    """raw = 0xD122: bit6=0 (CLR_LAST_EN=0), bit12=1 (RST_DEASS_CFG=1)."""
    _, decoded = explain_lines[0xF5]
    assert "CLR_LAST_EN=0" in decoded
    assert "RST_DEASS_CFG=1" in decoded


# Decoder coverage -- ensure decoders fire on all expected regs
EXPECTED_DECODED = {
    # reg : substring that must appear in the decoded column
    0x02: "OPER=",
    0x10: "unlocked",
    0x20: "exp=",
    0x21: "mV",
    0x24: "mV",
    0x25: "mV",
    0x26: "mV",
    0x2B: "mV",
    0x35: "mV",
    0x36: "mV",
    0x4F: " C",
    0x51: " C",
    0x60: "us",
    0x61: "us",
    0x64: "us",
    0x65: "us",
    0x79: "",        # decoder result varies but should be present
    0x88: "mV",
    0x8B: "mV",
    0x8C: "mA",
    0x8D: " C",
    0xEE: "lock",
    0xF5: "RST_DEASS_CFG=",
    0xFB: "FAULT",
}


@pytest.mark.parametrize("reg,expect_in", sorted(EXPECTED_DECODED.items()))
def test_decoder_fires_for_register(explain_lines, reg, expect_in):
    assert reg in explain_lines, f"reg 0x{reg:02X} missing from explain output"
    _, decoded = explain_lines[reg]
    if expect_in:
        assert expect_in in decoded, \
            f"reg 0x{reg:02X}: expected `{expect_in}` in `{decoded}`"
