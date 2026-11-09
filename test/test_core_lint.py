#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Check that Core lint path adaptation detects real app violations."""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--core", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    args = parser.parse_args()
    args.build_dir.mkdir(parents=True, exist_ok=True)
    cases = {
        "clean": ("qml/clean.cpp", "int value{0};\n", "boost_assert", 0),
        "missing-config": ("qml/clean.cpp", "#ifdef ENABLE_WALLET\n#endif\n", "includes_build_config", 1),
        "unused-import": ("test/functional/probe.py", "import os\n", "py_lint", 1),
        "unsafe-cleanup": ("test/functional/probe.py", "shutil.rmtree(path)\n", "rmtree", 1),
        "framework-cleanup": ("test/functional/qml_test_harness.py", "shutil.rmtree(path)\n", "rmtree", 0),
        "filesystem": ("qml/clean.cpp", "bool value{std::filesystem::exists(path)};\n", "std_filesystem", 1),
        "recursive-removal": ("qml/clean.cpp", "fs::remove_all(path);\n", "remove_all", 1),
        "boost-assert": ("qml/clean.cpp", "BOOST_ASSERT(value);\n", "boost_assert", 1),
        "trailing-space": ("qml/clean.cpp", "int value{0}; \n", "trailing_whitespace", 1),
    }
    for name, (file, content, check, expected) in cases.items():
        with tempfile.TemporaryDirectory(prefix="core-lint-", dir=args.build_dir) as temporary:
            root = Path(temporary)
            base_files = {
                "qml/config.cpp": "#include <bitcoin-build-config.h> // IWYU pragma: keep\n#ifdef ENABLE_WALLET\n#endif\n",
                "qml/clean.cpp": "int value{0};\n",
                "test/functional/probe.py": "value = 1\n",
                "ruff.toml": f'extend = "{args.core.resolve()}/ruff.toml"\n',
            }
            for path, text in base_files.items():
                target = root / path
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text(text, encoding="utf-8")
            header = root / "bitcoin/cmake/bitcoin-build-config.h.in"
            header.parent.mkdir(parents=True)
            header.write_bytes((args.core / "cmake/bitcoin-build-config.h.in").read_bytes())
            for command in (["git", "init", "--quiet"], ["git", "add", "."],
                            ["git", "-c", "user.name=Lint test", "-c", "user.email=lint@example.invalid",
                             "commit", "--quiet", "-m", "Lint fixture"]):
                subprocess.run(command, cwd=root, check=True)
            (root / file).write_text(content, encoding="utf-8")
            subprocess.run(["git", "add", file], cwd=root, check=True)
            result = subprocess.run([str(args.runner.resolve()), f"--lint={check}"], cwd=root,
                                    env={**os.environ, "COMMIT_RANGE": "HEAD..HEAD"},
                                    capture_output=True, text=True, check=False)
            if result.returncode != expected:
                print(f"{name}: expected {expected}, got {result.returncode}\n{result.stdout}{result.stderr}")
                return 1
            print(f"Core lint gate: {name} passed", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
