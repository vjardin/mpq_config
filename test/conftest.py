# SPDX-License-Identifier: BSD-4-Clause
# Copyright 2026 Vincent Jardin, Free Mobile <vjardin@free.fr>
"""Shared fixtures for the mpq_config test suite.

Env vars (all optional, sensible defaults):

  MPQ_CONFIG_BIN     path to the mpq_config binary
  MPQ_TEST_DATA      path to the test-data/ fixture dir
                     (default: ../test-data)
  MPQ_SERIAL_PORT    /dev/ttyUSB0 etc; when set, target tests run
  MPQ_SERIAL_BAUD    baud (default: 115200)
  MPQ_BUS            i2c bus on the board (default: 0)
  MPQ_ADDR           chip address (default: 0x10)

Without MPQ_SERIAL_PORT, target-marked tests skip; host-marked tests
always run against fixtures in test-data/."""

import os
import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


def pytest_configure(config):
    config.addinivalue_line(
        "markers",
        "host: runs locally against fixtures (no board required)"
    )
    config.addinivalue_line(
        "markers",
        "target: requires MPQ_SERIAL_PORT (board console over serial)"
    )


@pytest.fixture(scope="session")
def mpq_config_bin():
    env = os.environ.get("MPQ_CONFIG_BIN")
    if env:
        if not os.access(env, os.X_OK):
            pytest.fail(f"MPQ_CONFIG_BIN={env} not executable")
        return env
    default = os.path.join(ROOT, "mpq_config")
    if not os.access(default, os.X_OK):
        pytest.fail(
            f"{default} not built; run `make` in {ROOT} or set MPQ_CONFIG_BIN"
        )
    return default


@pytest.fixture(scope="session")
def test_data():
    p = os.environ.get("MPQ_TEST_DATA",
                       os.path.join(ROOT, "test-data"))
    if not os.path.isdir(p):
        pytest.fail(f"test-data dir not found: {p}")
    return p


@pytest.fixture(scope="session")
def fixtures(test_data):
    return {
        "live_dmp":    os.path.join(test_data, "u2200-live.dmp"),
        "live_csv":    os.path.join(test_data, "u2200-live.csv"),
        "live_mps":    os.path.join(test_data, "u2200-live.mps.txt"),
        "factory_txt": os.path.join(test_data, "factory.txt"),
    }


@pytest.fixture(scope="session")
def board():
    """Lazily open the serial console. Skips target tests if
    MPQ_SERIAL_PORT is unset or the port can't be opened."""
    port = os.environ.get("MPQ_SERIAL_PORT")
    if not port:
        pytest.skip("MPQ_SERIAL_PORT unset; target tests skipped")
    baud = int(os.environ.get("MPQ_SERIAL_BAUD", "115200"))
    from _board import Board
    b = Board(port, baud)
    yield b
    b.close()


@pytest.fixture(scope="session")
def chip_addr():
    return int(os.environ.get("MPQ_ADDR", "0x10"), 0)


@pytest.fixture(scope="session")
def chip_bus():
    return int(os.environ.get("MPQ_BUS", "0"))
