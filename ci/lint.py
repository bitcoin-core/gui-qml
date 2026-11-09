#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Run app and pinned Core lint with Core's CI dependencies."""

import argparse
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scope", choices=("all", "app", "core"), default="all")
    parser.add_argument("--image", default="bitcoinqml-linter")
    parser.add_argument("--no-build", action="store_true", help="Use an existing Core lint image")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    if not args.no_build:
        subprocess.run(["docker", "build", "--tag", args.image,
                        "--file", str(root / "bitcoin/ci/lint_imagefile"), str(root / "bitcoin")], check=True)
    command = ["docker", "run", "--rm", "--volume", f"{root}:{root}", "--workdir", str(root)]
    mounts = set()
    for repo in (root, root / "bitcoin"):
        for option in ("--absolute-git-dir", "--git-common-dir"):
            directory = Path(subprocess.check_output(
                ["git", "rev-parse", "--path-format=absolute", option], cwd=repo, text=True).strip())
            mounts.add(directory)
            stores = [directory / "objects"]
            while stores:
                store = stores.pop()
                alternates = store / "info/alternates"
                if alternates.is_file():
                    for line in alternates.read_text(encoding="utf-8").splitlines():
                        alternate = (store / line).resolve()
                        if alternate not in mounts:
                            mounts.add(alternate)
                            stores.append(alternate)
    for directory in sorted(mounts):
        if not directory.is_relative_to(root):
            command += ["--volume", f"{directory}:{directory}"]
    command += ["--env", "GIT_CONFIG_COUNT=1", "--env", "GIT_CONFIG_KEY_0=safe.directory",
                "--env", "GIT_CONFIG_VALUE_0=*", "--env", "PATH=/python_env/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"]
    if "COMMIT_RANGE" in os.environ:
        command += ["--env", f"COMMIT_RANGE={os.environ['COMMIT_RANGE']}"]
    command += ["--entrypoint", "/python_env/bin/python3", args.image,
                str(root / "ci/lint/runner.py"), "--scope", args.scope]
    return subprocess.call(command)


if __name__ == "__main__":
    raise SystemExit(main())
