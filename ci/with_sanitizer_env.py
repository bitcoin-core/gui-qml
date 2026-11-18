#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Run app tests with the pinned Core sanitizer defaults and narrow app rules."""

import argparse
import os
from pathlib import Path
import subprocess

from core_sanitizer_env import ROOT, core_sanitizer_env
from lsan_suppressions import lsan_options


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if not args.command:
        parser.error("No command selected")
    build = args.build_dir.resolve()
    defaults = core_sanitizer_env(build)
    defaults["LSAN_OPTIONS"] = lsan_options(build)
    # Preserve Core's UBSan rules and the existing app SQLite rule, unchanged.
    options = defaults["UBSAN_OPTIONS"].split(":")
    indices = [i for i, value in enumerate(options) if value.startswith("suppressions=")]
    if len(indices) != 1:
        raise RuntimeError("Core UBSan suppression options changed")
    index = indices[0]
    core_file = Path(options[index].removeprefix("suppressions="))
    combined = build / "sanitizer_suppressions/ubsan"
    combined.parent.mkdir(parents=True, exist_ok=True)
    combined.write_text(core_file.read_text(encoding="utf-8") + "\n" +
                        (ROOT / "test/sanitizer_suppressions/ubsan").read_text(encoding="utf-8"), encoding="utf-8")
    options[index] = "suppressions=" + str(combined)
    defaults["UBSAN_OPTIONS"] = ":".join(options)
    env = dict(os.environ)
    for name, value in defaults.items():
        env.setdefault(name, value)
    return subprocess.call(args.command, env=env)


if __name__ == "__main__":
    raise SystemExit(main())
