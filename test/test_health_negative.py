# SPDX-License-Identifier: BSD-4-Clause
# Copyright 2026 Vincent Jardin, Free Mobile <vjardin@free.fr>
"""Negative-health tests: a synthetic faulted dump is checked
against the **inverse** of each predicate in test_health.py.

Without this file the health predicates can rot silently:
test_health.py only proves "a known-good chip is in spec", not
"the test would correctly reject an out-of-spec chip". This pairs
each healthy-side assertion with its negative twin so the
predicate's discriminating power is itself under test."""

import os
import re
import pytest

pytestmark = pytest.mark.host


HEALTHY_VIN_MV = (11000, 13200)
HEALTHY_VOUT_MV = (780, 920)
HEALTHY_IOUT_MA = (0, 40000)
HEALTHY_TEMP_C = (-10, 105)

STATUS_WORD_FATAL_BITS = {
    15: "VOUT", 14: "IOUT_POUT", 13: "INPUT", 11: "POWER_NOT_GOOD",
    7: "BUSY", 4: "CML", 3: "VOUT_OV", 2: "IOUT_OC", 1: "VIN_UV",
}


@pytest.fixture(scope="module")
def faulted_reg(test_data):
    path = os.path.join(test_data, "u2200-faulted.dmp")
    table = {}
    with open(path) as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            m = re.match(
                r"^\s*0x([0-9A-Fa-f]+)\s+(\d+)\s+0x([0-9A-Fa-f]+)", line)
            if m:
                table[int(m.group(1), 16)] = int(m.group(3), 16)

    def get(r):
        return table.get(r)
    return get


def test_synthetic_status_word_has_fatal_bits(faulted_reg):
    v = faulted_reg(0x79)
    fatal = [n for b, n in STATUS_WORD_FATAL_BITS.items() if v & (1 << b)]
    assert fatal, f"synthetic STATUS_WORD=0x{v:04X} should have fatal bits"


def test_synthetic_vin_outside_envelope(faulted_reg):
    raw = faulted_reg(0x88)
    mv = raw * 25
    assert not (HEALTHY_VIN_MV[0] <= mv <= HEALTHY_VIN_MV[1]), \
        f"synthetic VIN raw=0x{raw:04X} = {mv} mV should be outside envelope"


def test_synthetic_vout_outside_envelope(faulted_reg):
    raw = faulted_reg(0x8B)
    mv = round(raw * 1000 / 512)
    assert not (HEALTHY_VOUT_MV[0] <= mv <= HEALTHY_VOUT_MV[1]), \
        f"synthetic VOUT raw=0x{raw:04X} = {mv} mV should be outside envelope"


def test_synthetic_iout_outside_envelope(faulted_reg):
    raw = faulted_reg(0x8C)
    ma = round(raw * 125 / 2)
    assert not (HEALTHY_IOUT_MA[0] <= ma <= HEALTHY_IOUT_MA[1]), \
        f"synthetic IOUT raw=0x{raw:04X} = {ma} mA should be outside envelope"


def test_synthetic_temperature_outside_envelope(faulted_reg):
    raw = faulted_reg(0x8D)
    c = raw if raw < 0x8000 else raw - 0x10000
    assert not (HEALTHY_TEMP_C[0] <= c <= HEALTHY_TEMP_C[1]), \
        f"synthetic TEMP raw=0x{raw:04X} = {c} C should be outside envelope"


def test_synthetic_vout_mode_not_linear_exp_minus_9(faulted_reg):
    v = faulted_reg(0x20)
    assert v != 0x17, \
        f"synthetic VOUT_MODE=0x{v:02X} should NOT match the target-board expected 0x17"


def test_synthetic_vout_scale_loop_not_canonical(faulted_reg):
    v = faulted_reg(0x29)
    assert v != 0x0400, \
        f"synthetic VOUT_SCALE_LOOP=0x{v:04X} should NOT be 0x0400"


def test_synthetic_mfr_addr_pmbus_wrong_window(faulted_reg):
    v = faulted_reg(0xD2)
    window = (v >> 4) & 0x7
    assert window != 0b001, \
        f"synthetic MFR_ADDR_PMBUS raw=0x{v:02X}, window should NOT be 0b001"


def test_synthetic_pmbus_revision_not_1_3(faulted_reg):
    v = faulted_reg(0x98)
    assert v != 0x33, f"synthetic PMBUS_REVISION=0x{v:02X} should NOT be 0x33"
