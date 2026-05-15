# SPDX-License-Identifier: BSD-4-Clause
# Copyright 2026 Vincent Jardin, Free Mobile <vjardin@free.fr>
"""On-target kernel-side coverage: i2c bus visibility, mpq8785
driver binding, hwmon sensor surface, debugfs knobs."""

import re
import pytest

pytestmark = pytest.mark.target


# Healthy envelope for `+0V8_VDD` (CPU core rail).
HEALTHY_VIN_MV = (11000, 13200)        # 12 V rail, +-10%
HEALTHY_VOUT_MV = (780, 920)           # 0V8 SoC core, +-15%
HEALTHY_IOUT_MA = (0, 40000)           # well below dual-phase max
HEALTHY_TEMP_MC = (-10000, 105000)     # in milli-degrees C (hwmon)

# Expected debugfs knobs (regression-guard against accidental removal).
EXPECTED_DEBUGFS_KNOBS = {
    "alarm_poll_interval_ms",
    "probe_smbus_rbyte",
    "probe_smbus_rword",
    "clear_protection_last",
    "clear_protection_last_force",
    "force_raw_xfer",
    "last_probe",
    "mfr_config_code_rev",
    "mfr_config_id",
    "protection_last",
    "reset_stats",
    "restore_all",
    "stats",
    "status_decoded",
    "store_all",
}


@pytest.fixture(scope="module", autouse=True)
def quiesce(board):
    """Make sure kernel tracing + alarm-poll worker are off so they
    don't interleave bytes on the serial console."""
    board.run("echo 0 > /sys/kernel/tracing/events/mpq8785/enable 2>/dev/null", t=3)
    board.run("echo 0 > /sys/kernel/debug/mpq8785/0-0010/alarm_poll_interval_ms",
              t=3)


def test_i2cdetect_shows_master(board):
    rc, body = board.run("i2cdetect -y 0 0x10 0x11", t=10)
    assert rc == 0
    assert "10:" in body


def test_driver_bound(board):
    rc, body = board.run(
        "test -d /sys/bus/i2c/drivers/mpq8785/0-0010 && echo BOUND", t=3)
    assert "BOUND" in body


def test_hwmon_node_exists(board):
    rc, body = board.run(
        "ls -d /sys/bus/i2c/drivers/mpq8785/0-0010/hwmon/hwmon* 2>/dev/null", t=3)
    paths = [L.strip() for L in body.splitlines() if "/hwmon/hwmon" in L]
    assert paths, f"no hwmon node under driver: {body!r}"


@pytest.fixture(scope="module")
def hwmon_readings(board):
    rc, body = board.run(
        "ls -d /sys/bus/i2c/drivers/mpq8785/0-0010/hwmon/hwmon* 2>/dev/null", t=3)
    path = next((L.strip() for L in body.splitlines()
                 if "/hwmon/hwmon" in L), None)
    assert path, "no hwmon path"
    rc, body = board.run(
        f"cat {path}/in1_input {path}/in2_input "
        f"{path}/curr1_input {path}/temp1_input", t=5)
    nums = re.findall(r"^(-?\d+)$", body, re.MULTILINE)
    assert len(nums) == 4, f"got {len(nums)} readings from cat: {body!r}"
    keys = ("in1_input", "in2_input", "curr1_input", "temp1_input")
    return dict(zip(keys, (int(n) for n in nums)))


def test_hwmon_vin_in_envelope(hwmon_readings):
    v = hwmon_readings["in1_input"]
    assert HEALTHY_VIN_MV[0] <= v <= HEALTHY_VIN_MV[1], \
        f"VIN={v} mV out of envelope"


def test_hwmon_vout_in_envelope(hwmon_readings):
    v = hwmon_readings["in2_input"]
    assert HEALTHY_VOUT_MV[0] <= v <= HEALTHY_VOUT_MV[1], \
        f"VOUT={v} mV out of envelope"


def test_hwmon_iout_in_envelope(hwmon_readings):
    v = hwmon_readings["curr1_input"]
    assert HEALTHY_IOUT_MA[0] <= v <= HEALTHY_IOUT_MA[1], \
        f"IOUT={v} mA out of envelope"


def test_hwmon_temperature_in_envelope(hwmon_readings):
    v = hwmon_readings["temp1_input"]
    assert HEALTHY_TEMP_MC[0] <= v <= HEALTHY_TEMP_MC[1], \
        f"TEMP={v/1000:.1f} C out of envelope"


def test_hwmon_consistency_with_chip(board, hwmon_readings):
    """hwmon should mirror what mpq_config reads from the chip. Same
    register, same scaling -- values should agree within a small
    LSB jitter from being read seconds apart."""
    rc, body = board.run(
        "mpq_config read --bus 0 --addr 0x10 --all --output /tmp/cmp.dmp; "
        "grep -E '^0x(88|8B|8C|8D) ' /tmp/cmp.dmp", t=15)
    raw = {}
    for line in body.splitlines():
        m = re.match(r"^\s*0x([0-9A-F]+)\s+\d+\s+0x([0-9A-F]+)", line)
        if m:
            raw[int(m.group(1), 16)] = int(m.group(2), 16)
    # Cross-check with the same LSB conventions hwmon uses.
    assert abs(raw[0x88] * 25 - hwmon_readings["in1_input"]) <= 200      # VIN
    # VOUT: hwmon prints mV; chip uses LINEAR16 exp=-9 (raw / 512 V).
    assert abs(round(raw[0x8B] * 1000 / 512)
               - hwmon_readings["in2_input"]) <= 5
    # IOUT 62.5 mA/LSB; chip workload jitters across reads taken
    # seconds apart -- allow ~2 A drift on a ~18 A baseline.
    assert abs(round(raw[0x8C] * 125 / 2)
               - hwmon_readings["curr1_input"]) <= 2000
    assert abs(raw[0x8D] * 1000
               - hwmon_readings["temp1_input"]) <= 2000


def test_debugfs_knobs_present(board):
    rc, body = board.run("ls /sys/kernel/debug/mpq8785/0-0010/", t=3)
    found = set(body.split())
    missing = EXPECTED_DEBUGFS_KNOBS - found
    assert not missing, f"debugfs missing knobs: {missing}"


def test_debugfs_alarm_poll_default_zero(board):
    rc, body = board.run(
        "cat /sys/kernel/debug/mpq8785/0-0010/alarm_poll_interval_ms", t=3)
    m = re.search(r"^\s*(\d+)\s*$", body, re.MULTILINE)
    assert m and int(m.group(1)) == 0


def test_debugfs_mfr_config_id_readable(board):
    rc, body = board.run(
        "cat /sys/kernel/debug/mpq8785/0-0010/mfr_config_id", t=3)
    # On the target board this is 0x0000 (the MPQ8646 personality 4-digit code)
    m = re.search(r"0x([0-9a-fA-F]+)", body)
    assert m is not None
