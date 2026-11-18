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
    parser.add_argument("--mode", "--profile", choices=("native", "tidy", "asan"), default="native")
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("cmake_options", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    extra = args.cmake_options[1:] if args.cmake_options[:1] == ["--"] else args.cmake_options
    root = Path(__file__).resolve().parent.parent
    command = ["cmake", "-S", str(root), "-B", str(args.build_dir), "-G", "Ninja",
               "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON", "-DBUILD_APP_TESTS=ON", "-DBUILD_DAEMON=ON",
               "-DENABLE_TEST_AUTOMATION=ON", "-DENABLE_IPC=ON"]
    if args.mode != "native":
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
    command += extra
    args.build_dir.mkdir(parents=True, exist_ok=True)
    (args.build_dir / "app-configure-command.json").write_text(json.dumps(command, indent=2) + "\n", encoding="utf-8")
    print(shlex.join(command), flush=True)
    return subprocess.call(command, cwd=root)


if __name__ == "__main__":
    raise SystemExit(main())
