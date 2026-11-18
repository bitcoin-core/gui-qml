#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Run a released Bitcoin-Qt next to bitcoin-core-app for compatibility tests."""

import os
import shlex
import shutil
import signal
import subprocess
import time
from pathlib import Path

from qml_wallet_test_lib import rpc_call


COMPAT_VERSION = "31.0"


def find_compat_bitcoin_qt():
    explicit = os.getenv("BITCOIN_QT_COMPAT")
    if explicit and os.path.isfile(explicit):
        return explicit

    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
    releases_root = os.getenv("PREVIOUS_RELEASES_DIR", os.path.join(repo_root, "releases"))
    candidate = os.path.join(releases_root, f"v{COMPAT_VERSION}", "bin", "bitcoin-qt")
    if os.path.isfile(candidate):
        return candidate

    return None


def process_output(process, output_directory):
    if not process or process.poll() is None:
        return ""
    stdout_path = Path(output_directory) / "bitcoin-qt.stdout.log"
    stderr_path = Path(output_directory) / "bitcoin-qt.stderr.log"
    stdout = stdout_path.read_text(encoding="utf-8", errors="replace") if stdout_path.exists() else ""
    stderr = stderr_path.read_text(encoding="utf-8", errors="replace") if stderr_path.exists() else ""
    if stdout and stderr:
        return f"stdout:\n{stdout}\n\nstderr:\n{stderr}"
    return stdout or stderr


def wait_for_rpc_or_process_exit(process, port, output_directory, timeout=45):
    deadline = time.time() + timeout
    last_error = None
    while time.time() < deadline:
        if process.poll() is not None:
            raise AssertionError(
                f"bitcoin-qt exited before RPC became ready with code {process.returncode}.\n"
                f"{process_output(process, output_directory)}"
            )
        try:
            rpc_call(port, "getblockcount")
            return
        except Exception as err:  # noqa: BLE001 - startup should tolerate transient RPC errors
            last_error = err
            time.sleep(0.25)
    raise AssertionError(f"bitcoin-qt RPC did not become ready before timeout: {last_error}")


def stop_bitcoin_qt(process, rpc_port):
    if not process:
        return
    if process.poll() is not None:
        return
    try:
        rpc_call(rpc_port, "stop")
    except Exception:
        process.send_signal(signal.SIGTERM)
    try:
        process.wait(timeout=20)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()


def bitcoin_qt_command(bitcoin_qt):
    runner = os.getenv("BITCOIN_QT_RUNNER")
    if runner:
        return shlex.split(runner) + [bitcoin_qt]
    xvfb_run = shutil.which("xvfb-run")
    if xvfb_run:
        return [xvfb_run, "-a", bitcoin_qt]
    if os.getenv("BITCOIN_QT_USE_DISPLAY") == "1" and os.getenv("DISPLAY"):
        return [bitcoin_qt]
    return None


def has_bitcoin_qt_runner():
    return bitcoin_qt_command("/bin/true") is not None


def bitcoin_qt_runner_skip_message():
    return (
        "SKIPPED: bitcoin-qt compatibility test needs DISPLAY, xvfb-run, "
        "or BITCOIN_QT_RUNNER. Set BITCOIN_QT_USE_DISPLAY=1 to use the "
        "ambient DISPLAY directly."
    )


def compat_bitcoin_qt_or_skip():
    """Return the released bitcoin-qt, or None after printing why the case skips."""
    bitcoin_qt = find_compat_bitcoin_qt()
    if not bitcoin_qt:
        print(f"SKIPPED: bitcoin-qt {COMPAT_VERSION} not found.")
        print(
            f"Set BITCOIN_QT_COMPAT or provide releases/v{COMPAT_VERSION}/bin/bitcoin-qt "
            "to exercise this flow."
        )
        return None
    if not has_bitcoin_qt_runner():
        print(bitcoin_qt_runner_skip_message())
        return None
    return bitcoin_qt


def require_bitcoin_qt_command(bitcoin_qt):
    command = bitcoin_qt_command(bitcoin_qt)
    if command is None:
        raise RuntimeError(bitcoin_qt_runner_skip_message())
    return command


def launch_bitcoin_qt(bitcoin_qt, datadir, output_directory, *, config_home=None, home=None, extra_args=()):
    """Start bitcoin-qt with output in output_directory and QSettings in config_home."""
    env = dict(os.environ)
    if config_home is not None:
        env["XDG_CONFIG_HOME"] = config_home
    else:
        env.setdefault("XDG_CONFIG_HOME", os.path.join(output_directory, "qt_compat_config"))
    os.makedirs(env["XDG_CONFIG_HOME"], exist_ok=True)
    if home is not None:
        os.makedirs(home, exist_ok=True)
        env["HOME"] = home
    command = require_bitcoin_qt_command(bitcoin_qt) + [
        f"-datadir={datadir}",
        "-nosplash",
        "-min",
        "-debug",
        "-debugexclude=leveldb",
        "-printtoconsole=1",
    ] + list(extra_args)
    if Path(command[0]).name == "xvfb-run":
        # The QML process may intentionally use minimal. Xvfb supplies an X11
        # display for Widgets, whose application fonts require that backend.
        env["QT_QPA_PLATFORM"] = "xcb"
    print(f"[bitcoin-qt] starting: {' '.join(command)}")
    print(f"[bitcoin-qt] Widgets QPA platform: {env.get('QT_QPA_PLATFORM', 'default')}")
    # Both streams can exceed pipe capacity during two-wallet startup/RPC.
    # Preserve full diagnostics on task-backed disk while the process runs;
    # no output reader or RPC may be responsible for unblocking Core's logger.
    stdout_path = Path(output_directory) / "bitcoin-qt.stdout.log"
    stderr_path = Path(output_directory) / "bitcoin-qt.stderr.log"
    print(f"[bitcoin-qt] output: {stdout_path}, {stderr_path}")
    with stdout_path.open("wb") as stdout, stderr_path.open("wb") as stderr:
        return subprocess.Popen(command, env=env, stdout=stdout, stderr=stderr)
