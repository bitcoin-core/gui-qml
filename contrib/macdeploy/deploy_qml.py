#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.
"""Deploy a shared-Qt QML app using the selected Qt installation."""

import argparse
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("macdeployqt")
    parser.add_argument("qt_library_dir", type=Path)
    parser.add_argument("qml_dir", type=Path)
    parser.add_argument("app", type=Path)
    args = parser.parse_args()
    binary = args.app / "Contents/MacOS/Bitcoin-Qt"

    # Homebrew's Qt libraries have absolute install names in separate package
    # prefixes. macdeployqt only carries *used* rpaths into its QML plugin scan.
    # Refer to the same libraries through the selected Qt library directory so
    # that directory remains available when resolving QML plugin dependencies.
    changes = []
    dependencies = subprocess.check_output(["otool", "-L", binary], text=True)
    for line in dependencies.splitlines()[1:]:
        library = Path(line.strip().split(" (compatibility version", 1)[0])
        if not library.is_absolute():
            continue
        relative = Path(library.name)
        for index, part in enumerate(library.parts):
            if part.endswith(".framework"):
                relative = Path(*library.parts[index:])
                break
        candidate = args.qt_library_dir / relative
        if candidate.is_file() and candidate.resolve() == library.resolve():
            changes += ["-change", str(library), f"@rpath/{relative}"]
    if changes:
        load_commands = subprocess.check_output(["otool", "-l", binary], text=True)
        rpaths = re.findall(r"cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset", load_commands)
        if str(args.qt_library_dir) not in rpaths:
            changes += ["-add_rpath", str(args.qt_library_dir)]
        subprocess.run(["install_name_tool", *changes, binary], check=True)

    result = subprocess.run(
        [args.macdeployqt, str(args.app), f"-qmldir={args.qml_dir}", "-no-codesign"],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, check=True,
    )
    print(result.stdout, end="", flush=True)
    # macdeployqt can report unresolved dependencies while returning success.
    if re.search(r"^ERROR:", result.stdout, re.MULTILINE):
        raise RuntimeError("Qt deployment reported an error")
    # Sign the complete dependency tree, including non-Qt Homebrew libraries.
    subprocess.run(["codesign", "--deep", "--force", "--sign", "-", args.app], check=True)
    subprocess.run(["codesign", "--verify", "--deep", "--strict", args.app], check=True)


if __name__ == "__main__":
    main()
