#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Regression test for bitcoin-qt loading QML-created wallet request metadata."""

import os
import shlex
import shutil
import signal
import subprocess
import sys
import time

from qml_driver import QmlDriverError
from qml_test_harness import dump_qml_tree
from qml_wallet_test_lib import WalletFlowHarness, rpc_call
from qml_test_receive import _create_request


COMPAT_VERSION = "31.0"
WALLET_NAME = "qt_compat_wallet"
REQUEST_AMOUNT = "0.001"
REQUEST_LABEL = "QML Qt Compat"
REQUEST_MESSAGE = "Compatibility request from bitcoin-core-app"
REQUEST_NOTE_SELF = "Private compatibility note"


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


def process_output(process):
    if not process or process.poll() is None:
        return ""
    stdout = process.stdout.read().decode("utf-8", errors="replace") if process.stdout else ""
    stderr = process.stderr.read().decode("utf-8", errors="replace") if process.stderr else ""
    if stdout and stderr:
        return f"stdout:\n{stdout}\n\nstderr:\n{stderr}"
    return stdout or stderr


def debug_log_tail(datadir, lines=200):
    debug_log = os.path.join(datadir, "regtest", "debug.log")
    if not os.path.isfile(debug_log):
        return ""
    with open(debug_log, "r", encoding="utf8", errors="replace") as log_file:
        return "".join(log_file.readlines()[-lines:])


def wait_for_rpc_or_process_exit(process, port, timeout=45):
    deadline = time.time() + timeout
    last_error = None
    while time.time() < deadline:
        if process.poll() is not None:
            raise AssertionError(
                f"bitcoin-qt exited before RPC became ready with code {process.returncode}.\n"
                f"{process_output(process)}"
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


def require_bitcoin_qt_command(bitcoin_qt):
    command = bitcoin_qt_command(bitcoin_qt)
    if command is None:
        raise RuntimeError(bitcoin_qt_runner_skip_message())
    return command


def launch_bitcoin_qt(bitcoin_qt, harness):
    env = dict(os.environ)
    env.setdefault("XDG_CONFIG_HOME", os.path.join(harness.tmpdir, "qt_compat_config"))
    os.makedirs(env["XDG_CONFIG_HOME"], exist_ok=True)
    command = require_bitcoin_qt_command(bitcoin_qt) + [
        f"-datadir={harness.gui_datadir}",
        "-nosplash",
        "-min",
        "-debug",
        "-debugexclude=leveldb",
        "-printtoconsole=1",
    ]
    print(f"[qml_wallet_qt_compat] starting bitcoin-qt: {' '.join(command)}")
    return subprocess.Popen(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def create_unencrypted_wallet(gui, wallet_name):
    try:
        gui.wait_for_property("createWalletButton", "visible", True, timeout_ms=1000)
        gui.wait_for_property("createWalletButton", "enabled", True, timeout_ms=25000)
        gui.click("createWalletButton")
    except QmlDriverError:
        pass
    gui.wait_for_property("walletTypeRegular", "visible", True, timeout_ms=5000)
    gui.click("walletTypeRegular")
    gui.wait_for_property("createWalletIntroStartButton", "visible", True, timeout_ms=5000)
    gui.click("createWalletIntroStartButton")
    gui.set_text("createWalletNameInput", wallet_name)
    gui.click("createWalletNameContinueButton")
    gui.wait_for_page("createWalletPasswordPage", timeout_ms=10000)
    gui.click("createWalletPasswordSkipButton")
    gui.wait_for_property("createWalletConfirmNextButton", "visible", True, timeout_ms=10000)
    gui.click("createWalletConfirmNextButton")
    gui.wait_for_property("createWalletBackupDoneButton", "visible", True, timeout_ms=10000)
    gui.click("createWalletBackupDoneButton")
    gui.wait_for_property("walletBadge", "text", wallet_name, timeout_ms=20000)


def create_full_payment_request(gui):
    gui.click("receiveTabButton")
    gui.wait_for_page("requestPaymentPage", timeout_ms=10000)
    _create_request(gui, REQUEST_AMOUNT, REQUEST_LABEL, REQUEST_MESSAGE, REQUEST_NOTE_SELF)
    gui.wait_for_property("requestPaymentTitle", "text", "Payment request", timeout_ms=10000)


def run_test():
    bitcoin_qt = find_compat_bitcoin_qt()
    if not bitcoin_qt:
        print(f"SKIPPED: bitcoin-qt {COMPAT_VERSION} not found.")
        print(
            f"Set BITCOIN_QT_COMPAT or provide releases/v{COMPAT_VERSION}/bin/bitcoin-qt "
            "to exercise this flow."
        )
        return 77
    if not has_bitcoin_qt_runner():
        print(bitcoin_qt_runner_skip_message())
        return 77

    harness = WalletFlowHarness("qml_wallet_qt_compat", port_offset=95)
    qt_process = None
    try:
        print(f"[qml_wallet_qt_compat] bitcoin-qt located: {bitcoin_qt}")
        harness.start_gui()
        gui = harness.driver
        print("[qml_wallet_qt_compat] QML GUI launched")
        harness.finish_onboarding()

        create_unencrypted_wallet(gui, WALLET_NAME)
        print("[qml_wallet_qt_compat] wallet created in bitcoin-core-app")

        create_full_payment_request(gui)
        print("[qml_wallet_qt_compat] payment request with all visible fields created")

        harness.stop_gui()
        print("[qml_wallet_qt_compat] bitcoin-core-app stopped")

        qt_process = launch_bitcoin_qt(bitcoin_qt, harness)
        wait_for_rpc_or_process_exit(qt_process, harness.gui_rpc_port)
        loaded_wallets = rpc_call(harness.gui_rpc_port, "listwallets")
        assert WALLET_NAME in loaded_wallets, f"Expected {WALLET_NAME} to load, got {loaded_wallets}"
        wallet_info = rpc_call(harness.gui_rpc_port, "getwalletinfo", wallet=WALLET_NAME)
        assert wallet_info["walletname"] == WALLET_NAME, f"Unexpected loaded wallet: {wallet_info}"

        time.sleep(3)
        if qt_process.poll() is not None:
            raise AssertionError(
                f"bitcoin-qt exited after loading wallet with code {qt_process.returncode}.\n"
                f"{process_output(qt_process)}"
            )

        print("[qml_wallet_qt_compat] bitcoin-qt stayed alive after loading QML-created request metadata")
        return 0
    except Exception as err:  # noqa: BLE001 - preserve failure context
        print(f"\nFAILED [qml_wallet_qt_compat]: {err}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        if harness.driver is not None:
            dump_qml_tree(harness.driver)
        qt_output = process_output(qt_process)
        if qt_output:
            print("\n--- bitcoin-qt process output ---", file=sys.stderr)
            print(qt_output, file=sys.stderr)
        log_tail = debug_log_tail(harness.gui_datadir)
        if log_tail:
            print("\n--- debug.log tail ---", file=sys.stderr)
            print(log_tail, file=sys.stderr)
        return 1
    finally:
        stop_bitcoin_qt(qt_process, harness.gui_rpc_port)
        harness.stop()


if __name__ == "__main__":
    sys.exit(run_test())
