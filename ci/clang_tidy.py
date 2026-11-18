#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Run Core's pinned clang-tidy checks against app compilation units."""

import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import re
import subprocess


class FileSelectionError(ValueError):
    pass


def resolve_requested_files(files, root, build):
    if files is None:
        return None
    if not files:
        raise FileSelectionError("Explicit file selection is empty")
    requested = set()
    for name in files:
        path = Path(name)
        path = (path if path.is_absolute() else root / path).resolve()
        if not path.is_relative_to(root):
            raise FileSelectionError(f"Requested file is outside the app root: {name}")
        if path.is_relative_to(root / "bitcoin"):
            raise FileSelectionError(f"Core files cannot be selected as app files: {name}")
        if path.is_relative_to(build):
            raise FileSelectionError(f"Generated build files cannot be selected explicitly: {name}")
        if path.suffix not in (".cpp", ".mm"):
            raise FileSelectionError(f"Expected a handwritten .cpp or .mm app file: {name}")
        if not path.is_file():
            raise FileSelectionError(f"Requested app file does not exist: {name}")
        requested.add(path)
    return requested


def select_entries(database, root, build, files=None):
    root, build = root.resolve(), build.resolve()
    requested = resolve_requested_files(files, root, build)
    entries = []
    compiled = set()
    seen = set()
    for entry in database:
        path = Path(entry.get("file", ""))
        if not path.is_absolute():
            path = Path(entry["directory"]) / path
        path = path.resolve()
        handwritten = path.suffix in (".cpp", ".mm") and path.is_relative_to(root) and not path.is_relative_to(root / "bitcoin") and not path.is_relative_to(build)
        moc = path.is_relative_to(build) and path.name == "mocs_compilation.cpp" and "bitcoinqml" in str(path)
        if handwritten:
            compiled.add(path)
        if not (moc or (handwritten and (requested is None or path in requested))) or path in seen:
            continue
        seen.add(path)
        entries.append({**entry, "file": str(path)})
    if requested is not None and requested - compiled:
        missing = ", ".join(str(path.relative_to(root)) for path in sorted(requested - compiled))
        raise FileSelectionError(f"Requested app files are not configured compilation units: {missing}")
    if not entries:
        raise FileSelectionError("No app compilation units selected")
    return entries


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, help="New artifact directory outside the build tree")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--fix", action="store_true", help="Apply clang-tidy's suggested fixes locally")
    parser.add_argument("--plugin", type=Path, help="Reuse a plugin built from the pinned Core source")
    parser.add_argument("--files", type=Path, nargs="+", metavar="PATH",
                        help="Check these configured handwritten app .cpp/.mm files (relative to the app root), retaining all app MOC aggregates")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    build = args.build_dir.resolve()
    try:
        requested = resolve_requested_files(args.files, root, build)
        database = json.loads(subprocess.check_output(["ninja", "-t", "compdb"], cwd=build, text=True))
        entries = select_entries(database, root, build, requested)
    except FileSelectionError as error:
        parser.error(str(error))
    config = root / "bitcoin/ci/test/00_setup_env_native_tidy.sh"
    version = re.search(r'TIDY_LLVM_V="(\d+)"', config.read_text(encoding="utf-8")).group(1)
    if args.plugin:
        plugin = args.plugin.resolve()
    else:
        plugin_build = build / "bitcoin-tidy"
        subprocess.run(["cmake", "-S", str(root / "bitcoin/contrib/devtools/bitcoin-tidy"), "-B", str(plugin_build),
                        f"-DLLVM_DIR=/usr/lib/llvm-{version}/cmake", "-DCMAKE_BUILD_TYPE=Release"], check=True)
        subprocess.run(["cmake", "--build", str(plugin_build), "--parallel", str(args.jobs), "--target", "bitcoin-tidy-tests"], check=True)
        plugin = plugin_build / "libbitcoin-tidy.so"
    output = args.output.resolve() if args.output else build / "clang-tidy"
    output.mkdir(parents=True, exist_ok=args.output is None)
    (output / "compile_commands.json").write_text(json.dumps(entries, indent=2) + "\n", encoding="utf-8")
    header_filter = "^" + re.escape(str(root)) + r"/(qml|test|cmake)/"

    def check(entry):
        path = entry["file"]
        command = [f"clang-tidy-{version}", "-p", str(output), f"--config-file={root}/bitcoin/src/.clang-tidy",
                   f"--load={plugin}", f"--header-filter={header_filter}", path]
        if args.fix:
            command.append("--fix")
        log_path = output / (str(Path(path).relative_to(root) if Path(path).is_relative_to(root) else Path(path).relative_to(build)).replace("/", "_") + ".log")
        with log_path.open("w", encoding="utf-8") as log:
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=False)
        print(f"{path}: {'PASS' if result.returncode == 0 else 'FAIL'}", flush=True)
        if result.returncode:
            print(log_path.read_text(encoding="utf-8"), flush=True)
        return {"file": path, "exit": result.returncode, "exit_code": result.returncode, "command": command, "log": str(log_path)}

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        results = list(pool.map(check, entries))
    (output / "results.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    return int(any(result["exit"] for result in results))


if __name__ == "__main__":
    raise SystemExit(main())
