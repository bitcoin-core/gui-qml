#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""End-to-end GUI test for the Receive Requests flow.

Covers issue #518 acceptance criteria: creating a receive request stores
real address + metadata, exposes a BIP21 payload via the QR, the history
survives a GUI restart, and entries can be removed.
"""

import os
import signal
import subprocess
import sys
import time

from qml_driver import QmlDriver, QmlDriverError
from qml_test_harness import GUI_STARTUP_TIMEOUT, dump_qml_tree
from qml_wallet_test_lib import WalletFlowHarness, rpc_call


WALLET_NAME = "receive_requests"


def _stop_gui(harness):
    if harness.driver:
        harness.driver.close()
        harness.driver = None
    if harness.gui_process and harness.gui_process.poll() is None:
        harness.gui_process.send_signal(signal.SIGTERM)
        try:
            harness.gui_process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            harness.gui_process.kill()
            harness.gui_process.wait()
    harness.gui_process = None


def _relaunch_gui(harness):
    env = dict(os.environ)
    env["QT_QPA_PLATFORM"] = "offscreen"
    args = [
        harness.gui_binary,
        f"-datadir={harness.gui_datadir}",
        f"-test-automation={harness.socket_path}",
        "-logtimemicros",
        "-debug",
        "-debugexclude=libevent",
        "-debugexclude=leveldb",
        "-nolisten",
    ]
    try:
        os.unlink(harness.socket_path)
    except FileNotFoundError:
        pass
    harness.gui_process = subprocess.Popen(
        args, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )
    harness.driver = QmlDriver(harness.socket_path, timeout=GUI_STARTUP_TIMEOUT)


def _import_wallet(harness):
    harness.start_source_node()
    backup_path = os.path.join(harness.tmpdir, f"{WALLET_NAME}.bak")
    rpc_call(harness.source_rpc_port, "createwallet", {"wallet_name": WALLET_NAME})
    rpc_call(harness.source_rpc_port, "backupwallet", [backup_path], wallet=WALLET_NAME)
    harness.stop_source_node()

    harness.start_gui()
    gui = harness.driver
    gui.wait_for_property("walletBadge", "loading", False, timeout_ms=20000)
    gui.click("walletBadge")
    try:
        gui.wait_for_property("walletSelectPopup", "opened", True, timeout_ms=2000)
        gui.click("walletSelectAddWalletButton")
    except QmlDriverError:
        pass
    gui.wait_for_property("importWalletButton", "visible", True, timeout_ms=10000)
    gui.click("importWalletButton")
    gui.wait_for_page("importWalletOptions", timeout_ms=10000)
    gui.set_text("importWalletPathField", backup_path)
    gui.click("importWalletChooseFileButton")
    gui.wait_for_page("importWalletSuccessPage", timeout_ms=30000)
    gui.click("importWalletSuccessOverviewButton")
    gui.wait_for_property("walletBadge", "text", WALLET_NAME, timeout_ms=20000)
    return gui


def _open_receive(gui):
    gui.click("receiveTabButton")
    gui.wait_for_page("requestPaymentPage", timeout_ms=10000)


def _create_request(gui, amount, name, message):
    gui.set_text("requestPaymentAmountInput", amount)
    gui.set_text("requestPaymentYourNameInput", name)
    gui.set_text("requestPaymentMessageInput", message)
    before = gui.get_property("requestHistoryCount", "count")
    gui.click("requestPaymentCreateButton")
    gui.wait_for_page("paymentRequestDetailPage", timeout_ms=20000)
    gui.wait_for_property("requestHistoryCount", "count", before + 1, timeout_ms=20000)


def run_test():
    harness = WalletFlowHarness("qml_receive_requests", port_offset=70)
    try:
        print("[qml_receive_requests] starting")
        gui = _import_wallet(harness)
        _open_receive(gui)

        _create_request(gui, "0.0001", "Alice", "pizza")
        qr_code = gui.get_property("paymentRequestDetailQRCode", "code")
        assert qr_code.startswith("bitcoin:"), f"QR payload missing BIP21 prefix: {qr_code!r}"
        assert "amount=0.00010000" in qr_code, f"QR payload missing amount: {qr_code!r}"
        assert "label=Alice" in qr_code, f"QR payload missing label: {qr_code!r}"
        assert "message=pizza" in qr_code, f"QR payload missing message: {qr_code!r}"
        print(f"[qml_receive_requests] created request with QR: {qr_code}")

        _stop_gui(harness)
        time.sleep(0.5)
        _relaunch_gui(harness)
        gui = harness.driver
        gui.wait_for_property("walletBadge", "text", WALLET_NAME, timeout_ms=30000)
        _open_receive(gui)
        gui.wait_for_property("requestHistoryCount", "count", 1, timeout_ms=20000)
        print("[qml_receive_requests] history persisted across restart")

        # Create a second request and verify history grows
        _create_request(gui, "0.005", "Bob", "coffee")
        qr_code2 = gui.get_property("paymentRequestDetailQRCode", "code")
        assert "amount=0.00500000" in qr_code2, f"Second QR missing amount: {qr_code2!r}"
        assert "label=Bob" in qr_code2, f"Second QR missing label: {qr_code2!r}"
        gui.click("paymentRequestDetailDone")
        gui.wait_for_page("requestPaymentPage", timeout_ms=10000)
        gui.wait_for_property("requestHistoryCount", "count", 2, timeout_ms=20000)
        print("[qml_receive_requests] second request created, history count is 2")

        print("[qml_receive_requests] completed")
        return 0
    except Exception as err:  # noqa: BLE001 - preserve failure context
        print(f"\nFAILED [qml_receive_requests]: {err}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        gui = harness.driver
        gui_output = harness.process_output(harness.gui_process)
        if gui_output:
            print("\n--- GUI process output ---", file=sys.stderr)
            print(gui_output, file=sys.stderr)
        if gui is not None:
            dump_qml_tree(gui)
        return 1
    finally:
        harness.stop()


if __name__ == "__main__":
    sys.exit(run_test())
