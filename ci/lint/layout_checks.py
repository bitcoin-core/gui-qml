#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Apply remaining pinned Python definitions with explicit app layout mappings."""

import importlib.util
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
CORE = ROOT / "bitcoin"
sys.path.insert(0, str(CORE / "test/lint"))


def check(name, policy):
    spec = importlib.util.spec_from_file_location(name, CORE / f"test/lint/{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    if name == "lint-includes":
        module.EXPECTED_BOOST_INCLUDES = policy["boost_includes"]
        original = module.check_output

        def output(command, **kwargs):
            if command[:3] == ["git", "ls-files", "src"]:
                command = ["git", "ls-files", "qml", "main.cpp", "test", "cmake", *command[3:]]
            return original(command, **kwargs)
        module.check_output = output
    elif name == "lint-include-guards":
        original = module._get_header_id
        module._get_header_id = lambda path: original("src/qml/" + path if path.startswith("test/") else "src/" + path)
    elif name == "check-doc":
        module.CMD_GREP_ARGS = shlex.join(["git", "grep", "--perl-regexp", module.REGEX_ARG,
                                         "--", "qml", "main.cpp", ":(exclude)qml/test/"])
        module.CMD_GREP_DOCS = shlex.join(["git", "grep", "--perl-regexp", module.REGEX_DOC, "--", "qml", "main.cpp"])
        module.CMD_GREP_DOCS += "; " + shlex.join(["git", "-C", str(CORE), "grep", "--perl-regexp", module.REGEX_DOC, "--", "src"])
        module.lint_missing_argument_documentation()
        return
    elif name == "lint-qt-translation":
        original = subprocess.run

        def run(command, **kwargs):
            return original(["qml" if argument == "src/qt" else argument for argument in command], **kwargs)
        module.subprocess.run = run
        try:
            module.main()
        finally:
            module.subprocess.run = original
        return
    module.main()


def main():
    os.chdir(ROOT)
    policy = json.loads((ROOT / "ci/lint-policy.json").read_text(encoding="utf-8"))
    files = subprocess.check_output(["git", "ls-files", "--", "*.cpp", "*.h", "*.mm"], text=True).splitlines()
    if not files:
        raise RuntimeError("No app sources selected")
    output = ROOT / "build/lint"
    output.mkdir(parents=True, exist_ok=True)
    (output / "layout-selection.json").write_text(json.dumps(files, indent=2) + "\n", encoding="utf-8")
    status = 0
    for name in ("check-doc", "lint-includes", "lint-include-guards", "lint-locale-dependence", "lint-qt-translation"):
        print("Running pinned Core", name, "on", len(files), "app files", flush=True)
        try:
            check(name, policy)
        except SystemExit as result:
            status |= int(result.code or 0)
    native = [file for file in files if Path(file).suffix in (".cpp", ".h")]
    result = subprocess.run([sys.executable, str(CORE / "contrib/devtools/circular-dependencies.py"), *native],
                            text=True, capture_output=True, check=False)
    (output / "dependency-cycles.log").write_text(result.stdout + result.stderr, encoding="utf-8")
    cycles = [line.removeprefix("Circular dependency: ") for line in result.stdout.splitlines() if line.startswith("Circular dependency: ")]
    if result.returncode not in (0, 1) or sorted(cycles) != sorted(policy["existing_cycles"]):
        print("App dependency inventory changed:", cycles, result.stderr)
        status = 1
    return status


if __name__ == "__main__":
    raise SystemExit(main())
