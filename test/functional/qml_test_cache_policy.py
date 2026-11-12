#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Verify that app launches do not use host-generated QML cache files."""

import ctypes
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time


STARTUP_SECONDS = 30
PREINIT_READY = "QML preinit window ready"
QML_CACHE_ENVIRONMENT = (
    "QML_DISABLE_DISK_CACHE",
    "QML_DISK_CACHE",
    "QML_DISK_CACHE_PATH",
    "QML_FORCE_DISK_CACHE",
)


class CacheReadMonitor:
    """Observe Linux opens/reads even when the cache bytes remain unchanged."""

    def __init__(self, path):
        self._fd = None
        self._accessed = False
        if not sys.platform.startswith("linux"):
            return
        libc = ctypes.CDLL(None, use_errno=True)
        libc.inotify_init1.argtypes = [ctypes.c_int]
        libc.inotify_init1.restype = ctypes.c_int
        libc.inotify_add_watch.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_uint32]
        libc.inotify_add_watch.restype = ctypes.c_int
        fd = libc.inotify_init1(os.O_NONBLOCK | os.O_CLOEXEC)
        if fd < 0:
            error = ctypes.get_errno()
            raise OSError(error, os.strerror(error))
        # IN_ACCESS | IN_OPEN: opening a stale unit is a policy violation even
        # if Qt rejects it without reading or overwriting the cached contents.
        if libc.inotify_add_watch(fd, os.fsencode(path), 0x00000001 | 0x00000020) < 0:
            error = ctypes.get_errno()
            os.close(fd)
            raise OSError(error, os.strerror(error), str(path))
        self._fd = fd

    def __enter__(self):
        return self

    def __exit__(self, *_):
        if self._fd is not None:
            os.close(self._fd)
            self._fd = None

    def was_accessed(self):
        if self._fd is not None:
            try:
                self._accessed |= bool(os.read(self._fd, 4096))
            except BlockingIOError:
                pass
        return self._accessed


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
    """Wait for the initial QML root, failing if startup stalls or exits."""
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
            "QT_LOGGING_RULES": env.get("QT_LOGGING_RULES", "") + "\nbitcoin.qml.startup.info=true",
            "LIBGL_ALWAYS_SOFTWARE": "1",
        }
    )
    if cache_path is not None:
        env["QML_DISK_CACHE_PATH"] = str(cache_path)
    if force_host_cache:
        # The application policy must take precedence over inherited settings.
        env["QML_DISK_CACHE"] = "qmlc"
        env["QML_FORCE_DISK_CACHE"] = "1"

    output_path = launch_root / "startup.log"
    # A file cannot fill up like an undrained PIPE and stall the child before
    # it reaches the QML document that this smoke test is meant to exercise.
    with output_path.open("w", encoding="utf-8") as output:
        process = subprocess.Popen(
            [str(gui_binary), "-regtest", f"-datadir={datadir}", "-nolisten", "-qml_onboarded=0"],
            env=env,
            stdout=output,
            stderr=subprocess.STDOUT,
            text=True,
        )
        try:
            deadline = time.monotonic() + STARTUP_SECONDS
            while time.monotonic() < deadline:
                diagnostics = output_path.read_text(encoding="utf-8", errors="replace")
                if process.poll() is not None:
                    diagnostics = output_path.read_text(encoding="utf-8", errors="replace")
                    raise AssertionError(
                        f"bitcoin-core-app exited during startup with {process.returncode}:\n{diagnostics}"
                    )
                if PREINIT_READY in diagnostics:
                    return
                time.sleep(0.1)
            raise AssertionError(
                f"bitcoin-core-app did not load preinit.qml within {STARTUP_SECONDS}s:\n"
                + output_path.read_text(encoding="utf-8", errors="replace")
            )
        finally:
            if process.poll() is None:
                process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()


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

        cold_cache = root / "cold-cache"
        cold_cache.mkdir()
        launch(gui_binary, root / "cold", cache_path=cold_cache)
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

        with CacheReadMonitor(stale_cache) as monitor:
            launch(
                gui_binary,
                root / "upgrade",
                cache_path=cache_path,
                force_host_cache=True,
            )
            if monitor.was_accessed():
                raise AssertionError("upgrade launch opened or read the stale QML cache file")
        if not sys.platform.startswith("linux"):
            print("Host-cache read monitoring is Linux-only; checking cache files and contents.")
        upgrade_cache_files = qml_cache_files(root)
        if upgrade_cache_files != [stale_cache]:
            raise AssertionError(
                f"upgrade launch changed the QML cache file set: {upgrade_cache_files}"
            )
        if stale_cache.read_bytes() != stale_contents:
            raise AssertionError("upgrade launch replaced the stale QML cache file")

    print("QML cache policy smoke test passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
