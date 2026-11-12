#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Configure app validation with the pinned Core CI toolchain and options."""

import argparse
import json
from pathlib import Path
import shlex
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=("tidy", "asan"), required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    config = root / f"bitcoin/ci/test/00_setup_env_native_{args.mode}.sh"
    values = subprocess.check_output(["bash", "-c", 'source "$1"; printf "%s\\0%s" "$APT_LLVM_V" "$BITCOIN_CONFIG"',
                                      "bash", str(config)], text=True).split("\0")
    version, options = values
    core_options = shlex.split(options)
    core_options = [option for option in core_options if option not in ("--preset", "--preset=dev-mode", "dev-mode")]
    presets = json.loads((root / "bitcoin/CMakePresets.json").read_text(encoding="utf-8"))
    preset = next(preset for preset in presets["configurePresets"] if preset["name"] == "dev-mode")
    features = [f"-D{name}={value}" for name, value in preset["cacheVariables"].items() if name.startswith(("ENABLE_", "WITH_"))]
    command = ["cmake", "-S", str(root), "-B", str(args.build_dir), "-G", "Ninja",
               "-DCMAKE_BUILD_TYPE=RelWithDebInfo", *features, *core_options,
               f"-DCMAKE_C_COMPILER=clang-{version}", f"-DCMAKE_CXX_COMPILER=clang++-{version}",
               "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON", "-DBUILD_APP_TESTS=ON",
               "-DBUILD_DAEMON=ON", "-DBUILD_CLI=ON", "-DENABLE_TEST_AUTOMATION=ON",
               "-DCMAKE_COMPILE_WARNING_AS_ERROR=ON", "-Werror=dev"]
    if args.mode == "asan":
        # Keep debug information and compiler memory use within hosted runners.
        command += ["-DCMAKE_C_FLAGS_RELWITHDEBINFO=-O1 -g1", "-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=-O1 -g1"]
    print(shlex.join(command), flush=True)
    return subprocess.call(command, cwd=root)


if __name__ == "__main__":
    raise SystemExit(main())
