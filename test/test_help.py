# SPDX-License-Identifier: BSD-4-Clause
# Copyright 2026 Vincent Jardin, Free Mobile <vjardin@free.fr>
"""help / usage / argument-validation surface."""

import subprocess
import pytest

pytestmark = pytest.mark.host

SUBCOMMANDS = [
    "read", "write", "to-csv", "from-csv", "to-mps", "from-mps",
    "diff", "explain", "live-diff",
]


def run_cli(mpq, *args, expect_rc=None):
    r = subprocess.run([mpq, *args], capture_output=True, text=True)
    if expect_rc is not None:
        assert r.returncode == expect_rc, \
            f"rc={r.returncode}: stdout={r.stdout!r} stderr={r.stderr!r}"
    return r


def test_help_lists_all_subcommands(mpq_config_bin):
    r = run_cli(mpq_config_bin, "help", expect_rc=0)
    combined = r.stdout + r.stderr
    for sub in SUBCOMMANDS:
        assert sub in combined, f"`{sub}` missing from help output"


def test_no_args_shows_usage(mpq_config_bin):
    r = run_cli(mpq_config_bin)
    assert r.returncode != 0
    assert "Usage:" in (r.stdout + r.stderr)


def test_unknown_subcommand_errors(mpq_config_bin):
    r = run_cli(mpq_config_bin, "do-the-needful")
    assert r.returncode != 0
    out = r.stdout + r.stderr
    assert "unknown subcommand" in out.lower() or "Usage:" in out


@pytest.mark.parametrize("sub", ["diff", "explain", "to-csv", "from-csv",
                                 "to-mps", "from-mps"])
def test_subcommand_without_args_errors(mpq_config_bin, sub):
    r = run_cli(mpq_config_bin, sub)
    assert r.returncode != 0, f"`{sub}` should error without args"


@pytest.mark.parametrize("sub", ["read", "write", "live-diff"])
def test_chip_subcommand_without_args_errors(mpq_config_bin, sub):
    r = run_cli(mpq_config_bin, sub)
    assert r.returncode != 0
    out = r.stdout + r.stderr
    assert "required" in out.lower() or "Usage:" in out
