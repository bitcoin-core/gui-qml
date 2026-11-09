#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Run Core's pinned clang-tidy checks against all app compilation units."""

import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--fix", action="store_true", help="Apply clang-tidy's suggested fixes locally")
    parser.add_argument("--plugin", type=Path, help="Reuse a plugin built from the pinned Core source")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    build = args.build_dir.resolve()
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
    database = json.loads(subprocess.check_output(["ninja", "-t", "compdb"], cwd=build, text=True))
    entries = []
    seen = set()
    for entry in database:
        path = Path(entry.get("file", ""))
        if not path.is_absolute():
            path = Path(entry["directory"]) / path
        path = path.resolve()
        if path.suffix != ".cpp" or not path.is_relative_to(root) or path.is_relative_to(root / "bitcoin"):
            continue
        if path.is_relative_to(build) or path in seen:
            continue
        seen.add(path)
        entry["file"] = str(path)
        entries.append(entry)
    if not entries:
        raise RuntimeError("No app compilation units selected")
    output = build / "clang-tidy"
    output.mkdir(exist_ok=True)
    (output / "compile_commands.json").write_text(json.dumps(entries, indent=2) + "\n", encoding="utf-8")
    header_filter = "^" + re.escape(str(root)) + r"/(qml|test|cmake)/"

    def check(entry):
        path = entry["file"]
        command = [f"clang-tidy-{version}", "-p", str(output), f"--config-file={root}/bitcoin/src/.clang-tidy",
                   f"--load={plugin}", f"--header-filter={header_filter}", path]
        if args.fix:
            command.append("--fix")
        log_path = output / (str(Path(path).relative_to(root)).replace("/", "_") + ".log")
        with log_path.open("w", encoding="utf-8") as log:
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=False)
        print(f"{Path(path).relative_to(root)}: {'PASS' if result.returncode == 0 else 'FAIL'}", flush=True)
        if result.returncode:
            print(log_path.read_text(encoding="utf-8"), flush=True)
        return {"file": path, "exit": result.returncode, "log": str(log_path)}

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        results = list(pool.map(check, entries))
    (output / "results.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    return int(any(result["exit"] for result in results))


if __name__ == "__main__":
    raise SystemExit(main())
