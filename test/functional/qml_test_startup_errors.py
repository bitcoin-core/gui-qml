#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Exercise actual startup error windows and preserve a locked profile's choices."""

import fcntl
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def run_case(binary, case):
    with tempfile.TemporaryDirectory(prefix="qml-startup-errors-") as temporary:
        root = Path(temporary)
        data = root / "data"
        network = data / "regtest"
        network.mkdir(parents=True)
        settings = network / "settings.json"
        original = {"prune": 0, "qml_onboarded": False}
        settings.write_text(json.dumps(original), encoding="utf8")
        config = root / "bitcoin.conf"
        config.write_text("", encoding="utf8")
        args = [
            binary, "-regtest", "-disablewallet", "-listen=0", f"-conf={config}",
            f"-test-settings-dir={root / 'gui'}",
        ]
        expected = "Invalid combination"
        if case == "command-line-networks":
            args += [f"-datadir={data}", "-testnet"]
        elif case == "configured-network":
            args += [f"-datadir={data}"]
            config.write_text("testnet=1\n", encoding="utf8")
        elif case == "missing-configured-directory":
            config.write_text(f"datadir={root / 'missing'}\n", encoding="utf8")
            expected = "does not exist"
        else:
            args += [f"-datadir={data}", "-qml_onboarded=0"]
            expected = "Cannot obtain a lock"
        env = dict(
            os.environ, QT_QPA_PLATFORM="minimal", QT_QUICK_BACKEND="software",
            QML_DISABLE_DISK_CACHE="1", HOME=str(root), XDG_CONFIG_HOME=str(root / "config"),
        )
        with (network / ".lock").open("w") as lock:
            if case == "held-directory":
                fcntl.lockf(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            result = subprocess.run(args, env=env, capture_output=True, text=True, timeout=30)
        assert result.returncode == 0, (case, result.returncode, result.stderr)
        assert "STARTUP_ERROR:" in result.stderr and expected in result.stderr, result.stderr
        saved = json.loads(settings.read_text(encoding="utf8"))
        saved.pop("_warning_", None)
        assert saved == original, (case, saved)
        print(f"Passed: {case}")


if __name__ == "__main__":
    for case in ("command-line-networks", "configured-network", "missing-configured-directory", "held-directory"):
        run_case(sys.argv[1], case)
