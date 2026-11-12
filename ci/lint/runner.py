#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Apply the pinned Core lint rules to both repository layouts."""

import argparse
import importlib.util
from importlib.metadata import version
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
CORE = ROOT / "bitcoin"
BUILD = ROOT / "build/lint"


def run(command, cwd=ROOT, **kwargs):
    print("+ " + " ".join(map(str, command)), flush=True)
    return subprocess.call(list(map(str, command)), cwd=cwd, **kwargs)


def lint_runner(core, target, env):
    subprocess.run(["cargo", "build", "--locked", "--manifest-path", str(core / "Cargo.toml"),
                    "--target-dir", str(target)], env=env, check=True)
    return target / "debug/test_runner"


def core_lint():
    env = dict(os.environ)
    # Let Core select its own history; the parent's range belongs to the app.
    env.pop("COMMIT_RANGE", None)
    runner = lint_runner(CORE / "test/lint/test_runner", BUILD / "core-target", env)
    return run([runner], cwd=CORE, env=env)


def app_lint():
    # Only repository paths differ. Check definitions and tool versions come
    # from the pinned submodule. Fail visibly if an upstream path changes.
    source = CORE / "test/lint/test_runner"
    checks = ("includes_build_config", "py_lint", "rmtree", "std_filesystem", "remove_all",
              "boost_assert", "scripted_diff", "commit_msg", "tabs_whitespace",
              "trailing_whitespace", "trailing_newline", "markdown", "doc_release_note_snippets")
    core_only = {"doc", "rpc_assert", "subtree", "all_python_linters"}
    available = set(re.findall(r'name:\s*"([^"]+)"', (source / "src/main.rs").read_text(encoding="utf-8")))
    if available != set(checks) | core_only:
        raise RuntimeError("Core lint checks changed; review the app check selection")
    adapted = BUILD / "app-runner"
    (adapted / "src").mkdir(parents=True, exist_ok=True)
    for name in ("Cargo.toml", "Cargo.lock"):
        shutil.copyfile(source / name, adapted / name)
    replacements = {
        '"./cmake/bitcoin-build-config.h.in"': '"bitcoin/cmake/bitcoin-build-config.h.in"',
        '"test/lint/commit-script-check.sh"': '"bitcoin/test/lint/commit-script-check.sh"',
        'test/functional/test_framework/test_framework.py': 'test/functional/qml_test_harness.py',
        '"./src/"': '"*.cpp", "*.h"',
        'md_ignore_paths.push("./doc/README_doxygen.md");': 'md_ignore_paths.push("./doc/README_doxygen.md");\n    md_ignore_paths.push("./bitcoin");',
    }
    counts = {old: 0 for old in replacements}
    for path in (source / "src").glob("*.rs"):
        text = path.read_text(encoding="utf-8")
        for old, new in replacements.items():
            counts[old] += text.count(old)
            text = text.replace(old, new)
        (adapted / "src" / path.name).write_text(text, encoding="utf-8")
    if any(count == 0 for count in counts.values()):
        raise RuntimeError(f"Core lint paths changed; update the app adapter: {counts}")
    env = dict(os.environ)
    runner = lint_runner(adapted, BUILD / "app-target", env)
    status = run([runner, *(f"--lint={name}" for name in checks)], env=env)
    sys.path.insert(0, str(CORE / "test/lint"))
    for name in ("lint-files", "lint-python", "lint-shell-locale"):
        spec = importlib.util.spec_from_file_location(name, CORE / f"test/lint/{name}.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        if name == "lint-files":
            get_metadata = module.get_git_file_metadata
            # Core prohibits submodules; this app deliberately has one pinned Core.
            module.get_git_file_metadata = lambda: {path: meta for path, meta in get_metadata().items() if path != "bitcoin"}
        if name == "lint-python":
            os.environ["MYPY_CACHE_DIR"] = str(BUILD / "mypy-cache")
            os.environ["MYPYPATH"] = str(CORE / "test/functional")
        print(f"Running Core {name} against the app", flush=True)
        try:
            module.main()
        except SystemExit as result:
            status |= int(result.code or 0)
    shell_files = subprocess.check_output(["git", "ls-files", "--", "*.sh"], cwd=ROOT, text=True).splitlines()
    if shell_files:
        status |= run([sys.executable, CORE / "test/lint/lint-shell.py"])
    else:
        print("ShellCheck: no tracked app shell scripts", flush=True)
    status |= run([sys.executable, ROOT / "test/functional/test_qml_test_directories.py"])
    status |= run([sys.executable, ROOT / "test/test_core_lint.py", "--runner", runner,
                   "--core", CORE, "--build-dir", BUILD / "gate-tests"])
    return status


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scope", choices=("all", "app", "core"), default="all")
    args = parser.parse_args()
    os.chdir(ROOT)
    BUILD.mkdir(parents=True, exist_ok=True)
    for tool in ("cargo", "ruff", "mypy", "mlc", "shellcheck"):
        if not shutil.which(tool):
            raise RuntimeError(f"Missing required Core CI lint dependency: {tool}")
    for package in ("lief", "mypy", "pyzmq"):
        version(package)
    status = 0
    if args.scope in ("all", "core"):
        status |= core_lint()
    if args.scope in ("all", "app"):
        status |= app_lint()
    return status


if __name__ == "__main__":
    raise SystemExit(main())
