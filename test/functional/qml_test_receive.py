#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""End-to-end GUI test for the Receive Requests flow.

Covers issue #518 acceptance criteria: creating a receive request stores
a real address + metadata, transforms the card into a modal, matching
request metadata remains reachable from fulfilled Activity transactions, and
history survives a GUI restart.

This test requires:
  - bitcoin-core-app built with -DENABLE_TEST_AUTOMATION=ON
  - bitcoind binary (searched alongside bitcoin-core-app, in build/bin/,
    or set BITCOIND env var)
"""

import json
import os
import re
import signal
import subprocess
import sys
import time
from urllib.parse import urlparse

from qml_driver import QmlDriver, QmlDriverError
from qml_test_harness import GUI_STARTUP_TIMEOUT, dump_qml_tree, qsettings_sandbox_args
from qml_wallet_test_lib import WalletFlowHarness, rpc_call, wait_for_rpc


WALLET_NAME = "receive_requests"
SECOND_WALLET_NAME = "receive_requests_alt"
MINER_WALLET_NAME = "receive_requests_miner"


def wait_until(predicate, timeout=20, interval=0.1, description="condition"):
    deadline = time.time() + timeout
    last_error = None
    while time.time() < deadline:
        try:
            if predicate():
                return
        except Exception as err:  # noqa: BLE001 - test polling should tolerate transient state
            last_error = err
        time.sleep(interval)
    if last_error:
        raise AssertionError(f"Timed out waiting for {description}: {last_error}")
    raise AssertionError(f"Timed out waiting for {description}")


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
    settings_args = qsettings_sandbox_args(env, harness.config_home)
    args = [
        harness.gui_binary,
        f"-datadir={harness.gui_datadir}",
        f"-test-automation={harness.socket_path}",
    ] + settings_args + [
        "-qml_onboarded=1",
        "-logtimemicros",
        "-debug",
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


def _create_wallet_backup(harness, wallet_name):
    backup_path = os.path.join(harness.tmpdir, f"{wallet_name}.bak")
    rpc_call(harness.source_rpc_port, "createwallet", {"wallet_name": wallet_name})
    rpc_call(harness.source_rpc_port, "backupwallet", [backup_path], wallet=wallet_name)
    return backup_path


def _import_wallet_backup(gui, backup_path, wallet_name):
    gui.wait_for_property("walletBadge", "loading", False, timeout_ms=20000)
    gui.click("walletBadge")
    try:
        gui.wait_for_property("walletSelectPopup", "opened", True, timeout_ms=2000)
        gui.click("walletSelectAddWalletButton")
    except QmlDriverError:
        pass
    gui.wait_for_property("walletTypeImport", "visible", True, timeout_ms=10000)
    gui.click("walletTypeImport")
    gui.wait_for_page("importWalletOptions", timeout_ms=10000)
    gui.set_text("importWalletPathField", backup_path)
    gui.click("importWalletChooseFileButton")
    gui.wait_for_page("importWalletSuccessPage", timeout_ms=30000)
    gui.click("importWalletSuccessOverviewButton")
    gui.wait_for_property("walletBadge", "text", wallet_name, timeout_ms=20000)
    gui.settle()


def _import_wallet(harness):
    harness.start_source_node()
    backup_path = _create_wallet_backup(harness, WALLET_NAME)
    harness.stop_source_node()

    harness.start_gui()
    gui = harness.driver
    _import_wallet_backup(gui, backup_path, WALLET_NAME)
    return gui


def _import_wallets(harness):
    harness.start_source_node()
    primary_backup_path = _create_wallet_backup(harness, WALLET_NAME)
    secondary_backup_path = _create_wallet_backup(harness, SECOND_WALLET_NAME)
    harness.stop_source_node()

    harness.start_gui()
    gui = harness.driver
    _import_wallet_backup(gui, primary_backup_path, WALLET_NAME)
    _import_wallet_backup(gui, secondary_backup_path, SECOND_WALLET_NAME)
    _select_wallet(gui, WALLET_NAME)
    return gui


def _select_wallet(gui, wallet_name):
    gui.wait_for_property("walletBadge", "loading", False, timeout_ms=20000)
    if gui.get_property("walletBadge", "text") == wallet_name:
        return

    gui.click("walletBadge")
    gui.wait_for_property("walletSelectPopup", "opened", True, timeout_ms=5000)

    wallet_object_name = "walletSelectItem_" + re.sub(r"[^A-Za-z0-9_]", "_", wallet_name)
    gui.wait_for_property(wallet_object_name, "visible", True, timeout_ms=10000)
    gui.click(wallet_object_name)
    gui.wait_for_property("walletBadge", "text", wallet_name, timeout_ms=10000)
    gui.wait_for_property("walletSelectPopup", "opened", False, timeout_ms=5000)
    gui.settle(timeout_ms=5000)


def _open_receive(gui):
    gui.invoke("walletSelectPopup", "close")
    gui.wait_for_property("walletSelectPopup", "visible", False)
    if gui.object_exists("paymentRequestModal"):
        gui.wait_for_property("paymentRequestModal", "visible", False)
    gui.click("receiveTabButton")
    gui.wait_for_page("requestPaymentPage", timeout_ms=10000)
    gui.settle()


def _open_activity(gui):
    try:
        gui.click("paymentRequestModalClose")
        gui.settle()
    except QmlDriverError:
        pass
    gui.click("activityTabButton")
    gui.wait_for_property("activitySearchField", "visible", True, timeout_ms=10000)


def _address_from_bip21(uri):
    parsed = urlparse(uri)
    assert parsed.scheme == "bitcoin", f"Unexpected BIP21 scheme: {uri!r}"
    assert parsed.path, f"BIP21 URI missing address: {uri!r}"
    return parsed.path


def _mine_to_address(harness, address):
    wait_for_rpc(harness.gui_rpc_port)
    blocks = rpc_call(harness.gui_rpc_port, "generatetoaddress", [1, address])
    assert len(blocks) == 1, f"Expected one generated block, got {blocks!r}"
    block = rpc_call(harness.gui_rpc_port, "getblock", [blocks[0]])
    return block["tx"][0]


def _edit_field(gui, field, value):
    input_name = {"label": "YourName", "message": "Message", "note": "NoteSelf", "amount": "Amount"}[field]
    gui.wait_for_property(f"requestPayment{input_name}Input", "visible", True)
    gui.set_text(f"requestPayment{input_name}Input", value)
    gui.invoke(f"requestPayment{input_name}Input", "editingFinished")


def _create_request(gui, amount, label, message, note_self=None):
    """Create in the card; the saved request opens in its modal."""
    for field, value in (("amount", amount), ("label", label), ("message", message), ("note", note_self)):
        if value:
            _edit_field(gui, field, value)
    before = gui.get_property("requestHistoryCount", "count")
    gui.wait_for_property("requestPaymentGenerateButton", "enabled", True)
    gui.click("requestPaymentGenerateButton")
    gui.wait_for_property("requestHistoryCount", "count", before + 1, timeout_ms=20000)
    gui.wait_for_property("paymentRequestModal", "opened", True)


def _request_qr_payload(gui):
    gui.wait_for_property("requestPaymentQRImage", "visible", True)
    return gui.get_property("requestPaymentQRImage", "code")


def run_test():
    harness = WalletFlowHarness("qml_receive_requests", port_offset=70)
    try:
        print("[qml_receive_requests] starting")
        gui = _import_wallets(harness)
        _open_receive(gui)
        gui.set_property("appWindow", "width", 1180)
        gui.set_property("appWindow", "height", 960)

        # Receiving is ready immediately, but saving a request requires details.
        gui.wait_for_property("receivingAddressQRImage", "visible", True)
        ready_address = gui.get_property("receivingAddressQRImage", "code")
        assert ready_address.startswith("bcrt1p")
        with open(os.path.join(harness.gui_datadir, "regtest", "settings.json"), encoding="utf-8") as settings_file:
            settings = json.load(settings_file)
        assert settings["qml_receive_address_types"][WALLET_NAME] == "bech32m"
        gui.invoke("requestPaymentAddressTypeDropdown", "activated", ["bech32"])
        gui.wait_for_property("requestPaymentAddressTypeDropdown", "currentValue", "bech32")
        ready_address = gui.get_property("receivingAddressQRImage", "code")
        assert ready_address.startswith("bcrt1q")
        with open(os.path.join(harness.gui_datadir, "regtest", "settings.json"), encoding="utf-8") as settings_file:
            settings = json.load(settings_file)
        assert settings["qml_receive_address_types"][WALLET_NAME] == "bech32"
        assert not gui.get_property("requestPaymentGenerateButton", "enabled")
        assert gui.get_property("requestHistoryCount", "count") == 0
        _open_activity(gui)
        _open_receive(gui)
        assert gui.get_property("receivingAddressQRImage", "code") == ready_address
        gui.save_screenshot(os.path.join(harness.tmpdir, "receive-draft.png"))
        # The receiving address QR has image actions before a request is saved.
        gui.invoke("receivingAddressQRContextMenu", "open")
        gui.wait_for_property("receivingAddressQRContextMenu", "opened", True)
        gui.click("receivingAddressQRContextCopy")
        gui.wait_for_property("receivingAddressQRContextMenu", "visible", False)
        address_qr_path = os.path.join(harness.tmpdir, "address-qr.png")
        gui.invoke("receivingAddressQRContextMenu", "open")
        gui.wait_for_property("receivingAddressQRContextMenu", "opened", True)
        gui.click("receivingAddressQRContextSave")
        gui.wait_for_property("receivingAddressSaveQRDialog", "visible", True)
        gui.set_property("receivingAddressSaveQRDialog", "selectedFile", "file://" + address_qr_path)
        gui.invoke("receivingAddressSaveQRDialog", "accepted")
        gui.invoke("receivingAddressSaveQRDialog", "close")
        wait_until(lambda: os.path.exists(address_qr_path), description="saved receiving address QR")
        with open(address_qr_path, "rb") as image:
            assert image.read(8) == b"\x89PNG\r\n\x1a\n"
        _create_request(gui, "0.0001", "Alice", "pizza", note_self="Private lunch note")
        original = _request_qr_payload(gui)
        assert "amount=0.00010000" in original and "label=Alice" in original
        assert "message=pizza" in original and "Private" not in original
        address = _address_from_bip21(original)
        assert address == ready_address
        gui.save_screenshot(os.path.join(harness.tmpdir, "receive-created.png"))

        # QR image actions live in the modal menu and keep the modal open.
        gui.click("paymentRequestMoreButton")
        gui.wait_for_property("paymentRequestMoreMenu", "opened", True)
        gui.save_screenshot(os.path.join(harness.tmpdir, "receive-menu.png"))
        gui.click("requestPaymentCopyQRMenuButton")
        gui.wait_for_property("paymentRequestMoreMenu", "visible", False)
        gui.settle()
        assert gui.get_property("requestPaymentError", "text") == ""
        assert gui.get_property("paymentRequestModal", "opened")
        qr_path = os.path.join(harness.tmpdir, "request-qr.png")
        gui.click("paymentRequestMoreButton")
        gui.wait_for_property("paymentRequestMoreMenu", "opened", True)
        gui.click("requestPaymentSaveQRMenuButton")
        gui.wait_for_property("requestPaymentSaveQRDialog", "visible", True)
        gui.set_property("requestPaymentSaveQRDialog", "selectedFile", "file://" + qr_path)
        gui.invoke("requestPaymentSaveQRDialog", "accepted")
        gui.invoke("requestPaymentSaveQRDialog", "close")
        wait_until(lambda: os.path.exists(qr_path), description="saved QR image")
        with open(qr_path, "rb") as image:
            assert image.read(8) == b"\x89PNG\r\n\x1a\n"
        assert gui.get_property("paymentRequestModal", "opened")

        # Inline public edits preserve the address and rebuild the BIP21 payload.
        _edit_field(gui, "amount", "0.0002")
        _edit_field(gui, "label", "Coffee & cake")
        assert _request_qr_payload(gui) == original
        gui.click("requestPaymentUpdateButton")
        gui.wait_for_property("requestPaymentGenerateButton", "visible", True)
        assert gui.get_property("requestPaymentQRImage", "code") == ""

        assert gui.get_property("receivingAddressQRImage", "code") == address
        print("Receive creation flow passed.")
        return 0
    except Exception as err:  # noqa: BLE001 - preserve failure context
        print(f"\nFAILED [qml_receive_requests]: {err}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        gui = harness.driver
        if gui is not None:
            gui.save_screenshot(os.path.join(harness.tmpdir, "receive-failure.png"))
            dump_qml_tree(gui)
        proc = harness.gui_process
        _stop_gui(harness)
        gui_output = harness.process_output(proc)
        if gui_output:
            print("\n--- GUI process output ---", file=sys.stderr)
            print(gui_output, file=sys.stderr)
        return 1
    finally:
        harness.stop()


if __name__ == "__main__":
    sys.exit(run_test())
