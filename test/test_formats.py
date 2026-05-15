# SPDX-License-Identifier: BSD-4-Clause
# Copyright 2026 Vincent Jardin, Free Mobile <vjardin@free.fr>
"""Native / CSV / MPS load + emit + round-trip coverage."""

import re
import subprocess
import pytest

pytestmark = pytest.mark.host

EXPECTED_LIVE_ONLY = {  # telemetry + factory-default-zero registers
    0x22, 0x40, 0x42, 0x43, 0x44, 0x58, 0x59,
    0x79, 0x88, 0x8B, 0x8C, 0x8D, 0x98, 0x9B, 0xFB,
}


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


def run(mpq, *args, expect_rc=0):
    r = subprocess.run([mpq, *args], capture_output=True, text=True)
    assert r.returncode == expect_rc, \
        f"{args}: rc={r.returncode} stderr={r.stderr}"
    return r


# Load + emit round-trips
def test_native_self_diff_clean(mpq_config_bin, fixtures):
    r = run(mpq_config_bin, "diff", fixtures["live_dmp"], fixtures["live_dmp"])
    assert "0 differences" in (r.stdout + r.stderr)


def test_csv_roundtrip(mpq_config_bin, fixtures, tmp_path):
    rt = tmp_path / "rt.dmp"
    run(mpq_config_bin, "from-csv", fixtures["live_csv"], str(rt))
    r = run(mpq_config_bin, "diff", fixtures["live_dmp"], str(rt))
    assert "0 differences" in (r.stdout + r.stderr)


def test_mps_roundtrip_non_telemetry(mpq_config_bin, fixtures, tmp_path):
    """MPS export filters out telemetry, so the round-trip preserves
    only the non-telemetry register set."""
    rt = tmp_path / "rt.dmp"
    run(mpq_config_bin, "from-mps", fixtures["live_mps"], str(rt))
    r = subprocess.run(
        [mpq_config_bin, "diff", fixtures["live_dmp"], str(rt)],
        capture_output=True, text=True)
    diff_out = r.stdout + r.stderr
    only_orig = re.findall(
        r'\s+0x([0-9A-F]+).*only in.*live\.dmp', diff_out)
    non_telem = [int(x, 16) for x in only_orig
                 if int(x, 16) not in EXPECTED_LIVE_ONLY]
    assert non_telem == [], \
        f"MPS round-trip dropped non-telemetry regs: {non_telem!r}"


def test_csv_emit_has_header(mpq_config_bin, fixtures, tmp_path):
    out = tmp_path / "out.csv"
    run(mpq_config_bin, "to-csv", fixtures["live_dmp"], str(out))
    with open(out) as f:
        first = f.readline().strip()
    assert first == "reg,width,value,name,clone_safe,notes"


def test_mps_emit_has_envelope(mpq_config_bin, fixtures, tmp_path):
    out = tmp_path / "out.txt"
    run(mpq_config_bin, "to-mps", fixtures["live_dmp"], str(out))
    body = out.read_text()
    for marker in ("END", "CRC_CHECK_START", "CRC_CHECK_STOP",
                   "Product ID:", "I2C Address:", "4-digit Code:",
                   "WRITE_PROTECTION"):
        assert marker in body, f"MPS export missing `{marker}`"


def test_mps_emit_is_tab_separated(mpq_config_bin, fixtures, tmp_path):
    """Every data row must have exactly 7 tab-separated fields,
    starting with `0000` `0` per the MPS Power Manager export format."""
    out = tmp_path / "out.txt"
    run(mpq_config_bin, "to-mps", fixtures["live_dmp"], str(out))
    data_rows = 0
    for line in out.read_text().splitlines():
        flds = line.split('\t')
        if len(flds) == 7 and flds[0] == "0000" and flds[1] == "0":
            data_rows += 1
    assert data_rows >= 60, f"too few MPS data rows: {data_rows}"


def test_to_mps_then_from_mps_is_subset_of_live(mpq_config_bin, fixtures,
                                                tmp_path):
    """live -> to-mps -> from-mps yields a subset (telemetry filtered)
    that is bit-exact for every reg it contains."""
    mps = tmp_path / "live.mps.txt"
    rt = tmp_path / "rt.dmp"
    run(mpq_config_bin, "to-mps", fixtures["live_dmp"], str(mps))
    run(mpq_config_bin, "from-mps", str(mps), str(rt))
    orig = parse_dump(fixtures["live_dmp"])
    back = parse_dump(rt)
    for reg, (width, val) in back.items():
        assert reg in orig, f"from-mps produced unknown reg 0x{reg:02X}"
        assert orig[reg][1] == val, \
            f"reg 0x{reg:02X}: orig=0x{orig[reg][1]:04X} back=0x{val:04X}"


# Auto-detect by extension
def test_diff_autodetect_mps(mpq_config_bin, fixtures):
    """diff should accept a .txt as one side and parse it as MPS."""
    r = subprocess.run([mpq_config_bin, "diff",
                        fixtures["factory_txt"], fixtures["live_dmp"]],
                       capture_output=True, text=True)
    # Expect a non-zero exit (there ARE drifts) and a clean count.
    m = re.search(r"(\d+) differences", r.stdout + r.stderr)
    assert m is not None, "diff didn't report a difference count"
    assert int(m.group(1)) > 0


def test_diff_autodetect_csv(mpq_config_bin, fixtures):
    r = subprocess.run([mpq_config_bin, "diff",
                        fixtures["live_csv"], fixtures["live_dmp"]],
                       capture_output=True, text=True)
    assert "0 differences" in (r.stdout + r.stderr)


# Loader empty/malformed guards
def test_load_native_empty_errors(mpq_config_bin, fixtures, tmp_path):
    empty = tmp_path / "empty.dmp"
    empty.write_text("# header only\n")
    r = subprocess.run([mpq_config_bin, "diff",
                        str(empty), fixtures["live_dmp"]],
                       capture_output=True, text=True)
    assert r.returncode != 0
    assert "parsed 0 entries" in (r.stdout + r.stderr)


def test_load_csv_empty_errors(mpq_config_bin, tmp_path):
    empty = tmp_path / "empty.csv"
    empty.write_text("reg,width,value,name,clone_safe,notes\n")
    r = subprocess.run([mpq_config_bin, "from-csv",
                        str(empty), "/dev/null"],
                       capture_output=True, text=True)
    assert r.returncode != 0
    assert "parsed 0 entries" in (r.stdout + r.stderr)


def test_load_mps_garbage_errors(mpq_config_bin, tmp_path):
    bad = tmp_path / "garbage.txt"
    bad.write_text("this is not an MPS export\nat all\n")
    r = subprocess.run([mpq_config_bin, "from-mps",
                        str(bad), "/dev/null"],
                       capture_output=True, text=True)
    assert r.returncode != 0
    assert "parsed 0 entries" in (r.stdout + r.stderr)


def test_load_native_missing_file_errors(mpq_config_bin, tmp_path):
    r = subprocess.run([mpq_config_bin, "diff",
                        str(tmp_path / "nope.dmp"),
                        str(tmp_path / "also-nope.dmp")],
                       capture_output=True, text=True)
    assert r.returncode != 0
