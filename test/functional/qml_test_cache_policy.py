#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Verify that app launches do not use host-generated QML cache files."""

import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time


STARTUP_SECONDS = 5
QML_CACHE_ENVIRONMENT = (
    "QML_DISABLE_DISK_CACHE",
    "QML_DISK_CACHE",
    "QML_DISK_CACHE_PATH",
    "QML_FORCE_DISK_CACHE",
)


def find_gui_binary():
    """Locate the bitcoin-core-app binary."""
    env_path = os.getenv("BITCOIN_CORE_APP")
    if env_path and Path(env_path).is_file():
        return Path(env_path)

    repo_root = Path(__file__).resolve().parents[2]
    build_path = repo_root / "build" / "bin" / "bitcoin-core-app"
    if build_path.is_file():
        return build_path

    raise FileNotFoundError(
        "Cannot find bitcoin-core-app. Set BITCOIN_CORE_APP or build the app first."
    )


def launch(gui_binary, launch_root, cache_path=None, force_host_cache=False):
    """Start the app long enough to load its initial QML document."""
    home = launch_root / "home"
    config = launch_root / "config"
    datadir = launch_root / "datadir"
    for path in (home, config, datadir):
        path.mkdir(parents=True)

    env = dict(os.environ)
    for name in QML_CACHE_ENVIRONMENT:
        env.pop(name, None)
    env.update(
        {
            "HOME": str(home),
            "XDG_CACHE_HOME": str(launch_root / "cache"),
            "XDG_CONFIG_HOME": str(config),
            "QT_QPA_PLATFORM": os.getenv(
                "QML_TEST_QPA_PLATFORM",
                "minimal" if sys.platform == "darwin" else "offscreen",
            ),
            "QT_QUICK_BACKEND": "software",
            "LIBGL_ALWAYS_SOFTWARE": "1",
        }
    )
    if cache_path is not None:
        env["QML_DISK_CACHE_PATH"] = str(cache_path)
    if force_host_cache:
        # The application policy must take precedence over inherited settings.
        env["QML_DISK_CACHE"] = "qmlc"
        env["QML_FORCE_DISK_CACHE"] = "1"

    process = subprocess.Popen(
        [
            str(gui_binary),
            "-regtest",
            f"-datadir={datadir}",
            "-nolisten",
        ],
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    deadline = time.monotonic() + STARTUP_SECONDS
    while time.monotonic() < deadline:
        if process.poll() is not None:
            output, _ = process.communicate()
            raise AssertionError(
                f"bitcoin-core-app exited during startup with {process.returncode}:\n{output}"
            )
        time.sleep(0.1)

    process.terminate()
    try:
        process.communicate(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill()
        process.communicate()


def qml_cache_files(root):
    """Return all host QML cache files below root."""
    return sorted(
        path
        for path in root.rglob("*")
        if path.is_file() and path.suffix in {".jsc", ".qmlc"}
    )


def main():
    gui_binary = find_gui_binary()
    with tempfile.TemporaryDirectory(prefix="qml_cache_policy_") as tmpdir:
        root = Path(tmpdir)

        launch(gui_binary, root / "cold")
        cold_cache_files = qml_cache_files(root)
        if cold_cache_files:
            raise AssertionError(f"cold launch wrote QML cache files: {cold_cache_files}")

        cache_path = root / "upgrade-cache"
        cache_path.mkdir()
        source_path = ":/qml/pages/preinit.qml"
        cache_name = hashlib.sha1(source_path.encode()).hexdigest() + ".qmlc"
        stale_cache = cache_path / cache_name
        stale_contents = b"stale host-generated QML cache"
        stale_cache.write_bytes(stale_contents)

        launch(
            gui_binary,
            root / "upgrade",
            cache_path=cache_path,
            force_host_cache=True,
        )
        upgrade_cache_files = qml_cache_files(root)
        if upgrade_cache_files != [stale_cache]:
            raise AssertionError(
                f"upgrade launch changed the QML cache file set: {upgrade_cache_files}"
            )
        if stale_cache.read_bytes() != stale_contents:
            raise AssertionError("upgrade launch read and replaced the stale QML cache file")

    print("QML cache policy smoke test passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
