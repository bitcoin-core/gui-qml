#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Discovery must be cheap, and failed fixture setup must fail the selected case."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    with tempfile.TemporaryDirectory(prefix="qml-wallet-fixture-") as directory:
        profiles = Path(directory)
        command = [sys.argv[1], "--module=psbt", "--test=feeReplacementRequiresConfirmation",
                   f"--profile-dir={profiles}"]
        discovery = subprocess.run([*command, "-datatags"], capture_output=True, text=True,
                                   check=True, timeout=20)
        assert "feeReplacementRequiresConfirmation" in discovery.stdout
        assert not list(profiles.iterdir()), "Discovery generated or retained a profile"

        # QCoreApplication discovery works; QApplication seed generation must fail.
        environment = dict(os.environ, QT_QPA_PLATFORM="nonexistent-fixture-test-platform")
        failure = subprocess.run(command, env=environment, capture_output=True, text=True, timeout=30)
        assert failure.returncode != 0, failure.stdout
        assert "Funded-wallet setup failed" in failure.stdout + failure.stderr
        seeds = list(profiles.glob("bitcoinqml-funded-seed-*"))
        assert len(seeds) == 1 and (seeds[0] / "stderr.txt").stat().st_size > 0

    print("Discovery creates no seed; setup failures fail the case and retain diagnostics.")


if __name__ == "__main__":
    main()
