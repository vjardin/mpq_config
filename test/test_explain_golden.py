# SPDX-License-Identifier: BSD-4-Clause
# Copyright 2026 Vincent Jardin, Free Mobile <vjardin@free.fr>
"""Byte-for-byte regression check on `mpq_config explain`.

The golden output captures every decoder's exact text against the
saved live dump. Any change in formatting, scaling, or new decoder
shows up as a diff. It catches accidental regressions in few ms,
without needing the chip.

Regenerate after an intentional decoder change:

  ./build/mpq_config explain test-data/u2200-live.dmp \\
      | tail -n +2 > test-data/u2200-live.explain.golden.txt
"""

import os
import subprocess
import pytest

pytestmark = pytest.mark.host


def test_explain_matches_golden(mpq_config_bin, fixtures, test_data):
    golden_path = os.path.join(test_data, "u2200-live.explain.golden.txt")
    golden = open(golden_path).read().splitlines()

    r = subprocess.run([mpq_config_bin, "explain", fixtures["live_dmp"]],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    # Skip the leading `# explain: <path>` line. It embeds the
    # absolute path that varies by checkout location.
    actual = r.stdout.splitlines()
    assert actual[0].startswith("# explain:"), \
        f"first line is no longer the explain banner: {actual[0]!r}"
    actual = actual[1:]

    if actual != golden:
        # Build a unified-diff-style summary so failures are easy
        # to triage. Show the first 20 mismatches.
        msg = ["explain output drifted from golden:"]
        a, b = actual, golden
        for i, (la, lb) in enumerate(zip(a, b)):
            if la != lb:
                msg.append(f"  line {i + 2}:")
                msg.append(f"    -golden: {lb!r}")
                msg.append(f"    +actual: {la!r}")
                if len(msg) > 60:
                    msg.append("  ... (truncated)")
                    break
        if len(a) != len(b):
            msg.append(f"  length: golden={len(b)} lines, "
                       f"actual={len(a)} lines")
        pytest.fail("\n".join(msg))
