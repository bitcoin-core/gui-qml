#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Print Core's LSan options with the app's dependency exceptions appended."""

import argparse
from pathlib import Path
import tempfile

from core_sanitizer_env import core_sanitizer_env


def lsan_options(build):
    root = Path(__file__).resolve().parent.parent
    options = core_sanitizer_env(build)["LSAN_OPTIONS"].split(":")
    paths = [i for i, option in enumerate(options) if option.startswith("suppressions=")]
    if len(paths) != 1:
        raise RuntimeError("Core LSan options changed; review suppression-file handling")
    index = paths[0]
    core_file = Path(options[index].removeprefix("suppressions="))
    combined = build / "sanitizer_suppressions/lsan"
    combined.parent.mkdir(parents=True, exist_ok=True)
    text = core_file.read_text(encoding="utf-8") + "\n" + (root / "test/sanitizer_suppressions/lsan").read_text(encoding="utf-8")
    with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=combined.parent, delete=False) as output:
        output.write(text)
    Path(output.name).replace(combined)
    options[index] = f"suppressions={combined}"
    return ":".join(options)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    args = parser.parse_args()
    print(lsan_options(args.build_dir.resolve()))


if __name__ == "__main__":
    main()
