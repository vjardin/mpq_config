# SPDX-License-Identifier: BSD-4-Clause
# Copyright 2026 Vincent Jardin, Free Mobile <vjardin@free.fr>
"""Factory NVM config (MPS Power Manager GUI export) vs live fixture.

The MPS GUI produces factory.txt with the
factory-burned MTP contents. Diffing it against the live chip should
surface only the known, explainable drifts."""

import re
import subprocess
import pytest

pytestmark = pytest.mark.host


# Telemetry + factory-default-zero registers that the MPS GUI export
# omits but the live `read --all` includes.
EXPECTED_LIVE_ONLY = {
    0x22, 0x40, 0x42, 0x43, 0x44, 0x58, 0x59,
    0x79, 0x88, 0x8B, 0x8C, 0x8D, 0x98, 0x9B, 0xFB,
}

# Known real value drifts (the chip's current state vs the factory file):
#   0xC2 MFR_PRODUCT_REV_USER -- operator-written, persists in user NVM
#   0xD2 MFR_ADDR_PMBUS       -- MPQ8646 personality overrides bits[6:4]
#   0xF8 CRC_USER             -- chip recomputes on user-NVM change
EXPECTED_DRIFT_REGS = {"C2", "D2", "F8"}
REQUIRED_DRIFT_REGS = {"C2", "D2"}  # CRC_USER may match if NVM untouched


@pytest.fixture(scope="module")
def diff_output(mpq_config_bin, fixtures):
    r = subprocess.run(
        [mpq_config_bin, "diff", fixtures["factory_txt"], fixtures["live_dmp"]],
        capture_output=True, text=True)
    return r.stdout + r.stderr


def test_diff_emits_count(diff_output):
    assert re.search(r"(\d+) differences", diff_output) is not None


def test_no_factory_regs_missing_from_inventory(diff_output, fixtures):
    """If we ever drop a register that the factory GUI exports, this
    will catch it. The inventory MUST cover every config reg the
    factory tool emits."""
    only_factory = re.findall(
        r"\s+0x([0-9A-F]+).*only in.*factory", diff_output)
    assert only_factory == [], f"factory regs missing: {only_factory}"


def test_only_in_live_is_telemetry_or_zero_default(diff_output):
    only_live = re.findall(r"\s+0x([0-9A-F]+).*only in.*live", diff_output)
    unexpected = [r for r in only_live
                  if int(r, 16) not in EXPECTED_LIVE_ONLY]
    assert unexpected == [], \
        f"live has non-telemetry regs the factory file lacks: {unexpected}"


def test_real_drifts_are_expected(diff_output):
    drifts = re.findall(
        r"^\s*0x([0-9A-F]+) (\S+)\s+0x[0-9A-F]+ -> 0x[0-9A-F]+",
        diff_output, re.MULTILINE)
    got = set(r for r, _ in drifts)
    unexpected = got - EXPECTED_DRIFT_REGS
    assert unexpected == set(), \
        f"unexpected real drifts: {unexpected} (full: {drifts})"
    missing = REQUIRED_DRIFT_REGS - got
    assert missing == set(), \
        f"required drifts not seen: {missing} (drifts: {drifts})"


def test_factory_file_parses_75_rows(mpq_config_bin, fixtures, tmp_path):
    out = tmp_path / "fac.dmp"
    r = subprocess.run(
        [mpq_config_bin, "from-mps", fixtures["factory_txt"], str(out)],
        capture_output=True, text=True)
    assert r.returncode == 0
    m = re.search(r"(\d+) entries MPS", r.stderr + r.stdout)
    assert m is not None
    assert int(m.group(1)) == 75
