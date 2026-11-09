#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Verify the Qt hash exception leaves other integer diagnostics active."""

import os
from pathlib import Path
import subprocess
import sys


def main():
    executable = Path(sys.argv[1]).resolve()
    env = os.environ.copy()
    env["UBSAN_OPTIONS"] = env.get("UBSAN_OPTIONS", "") + ":halt_on_error=1:print_stacktrace=1"
    cases = {
        "qt-hash": None,
        "unsigned": "runtime error: unsigned integer overflow",
        "signed": "runtime error: signed integer overflow",
        "truncation": "runtime error: implicit conversion",
        "neighbor": "runtime error: unsigned integer overflow",
        "hash-signed": "runtime error: signed integer overflow",
    }
    for name, diagnostic in cases.items():
        result = subprocess.run([str(executable), name], env=env, capture_output=True, text=True, check=False)
        if diagnostic is None:
            valid = result.returncode == 0 and not result.stderr
        else:
            valid = result.returncode != 0 and diagnostic in result.stderr and "runtime error:" in result.stderr
        if not valid:
            print(f"{name}: unexpected exit status {result.returncode}\n{result.stdout}{result.stderr}")
            return 1
        print(f"Sanitizer suppression control: {name} passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
