# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Shared process checks and UI actions for short GUI smoke journeys."""

import os
import time
from pathlib import Path
from qml_driver import QmlDriverError
from qml_process_checks import (
    check_gui_exit,
    report_gui_failure,
)
from qml_test_harness import dump_qml_tree
from qml_wallet_test_lib import (
    WalletFlowHarness,
    open_wallet_selector,
    wait_for_loaded_wallets,
)


def wait_until(predicate, description, timeout=30):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.1)
    raise AssertionError(f"Timed out waiting for {description}")


def stop_gui_checked(harness):
    process = harness.gui_process
    harness.stop_gui()
    if process is not None:
        _, stderr = process.communicate()
        check_gui_exit(process.returncode, stderr.decode("utf-8", errors="replace"), socket_path=harness.socket_path)


def wallet_scenario(name, stage, action):
    from qml_scenarios import Scenario
    return Scenario(name, stage, lambda options: run_smoke_case(name, action, options.save_screenshots))


def run_smoke_case(name, scenario, save_screenshots=False):
    harness = WalletFlowHarness(name, port_offset=0)
    artifacts = Path(os.environ.get("QML_TEST_ARTIFACTS", Path(__file__).resolve().parents[2] / "build/test-artifacts")) / name
    index = 0

    def checkpoint(label):
        nonlocal index
        index += 1
        print(f"[{name}] {index}: {label}", flush=True)
        if save_screenshots:
            artifacts.mkdir(parents=True, exist_ok=True)
            harness.driver.save_screenshot(str(artifacts / f"{index:02}.png"))

    try:
        scenario(harness, checkpoint)
        stop_gui_checked(harness)
        print(f"[{name}] PASSED", flush=True)
    except Exception:
        if harness.driver is not None:
            try:
                dump_qml_tree(harness.driver)
            except QmlDriverError:
                pass
        report_gui_failure(harness.gui_process, name)
        raise
    finally:
        harness.stop()


def open_wallet_settings(gui):
    gui.click("desktopWalletSettingsTabButton")
    gui.wait_for_property("desktopWalletSettingsTabButton", "checked", True)
    gui.click("settingsSidebar_wallet")
    gui.wait_for_page("walletSettingsPage")


def close_wallet(harness, wallet_name):
    gui = harness.driver
    open_wallet_selector(gui)
    gui.click(f"walletSelectActions_{wallet_name}")
    gui.click(f"walletSelectClose_{wallet_name}")
    gui.wait_for_property("walletCloseConfirmationPopup", "opened", True)
    gui.click("walletCloseConfirmationConfirmButton")
    gui.wait_for_property("walletCloseConfirmationPopup", "visible", False)
    wait_for_loaded_wallets(harness.gui_rpc_port, [])


def create_wallet(harness, name, password):
    gui = harness.driver
    harness.open_create_wallet_wizard()
    if gui.object_exists("createWalletButton") and gui.get_property("createWalletButton", "visible"):
        gui.click("createWalletButton")
    gui.wait_for_property("walletTypeRegular", "visible", True)
    gui.click("walletTypeRegular")
    gui.wait_for_page("createWalletFormPage")
    gui.set_text("createWalletNameInput", name)
    gui.set_text("createWalletPasswordInput", password)
    gui.set_text("createWalletPasswordRepeatInput", password)
    gui.click("createWalletPasswordConfirmToggle")
    gui.wait_for_property("createWalletFormCreateButton", "enabled", True)
    gui.click("createWalletFormCreateButton")
    gui.wait_for_page("walletCreationReadyPage", timeout_ms=30000)
    gui.click("createWalletReadyDoneButton")
    gui.wait_for_property("walletBadge", "text", name, timeout_ms=30000)


def import_wallet(harness, backup_path):
    harness.open_create_wallet_wizard()
    gui = harness.driver
    gui.wait_for_property("walletTypeImport", "visible", True)
    gui.set_text("importWalletPathField", str(backup_path))
    gui.click("walletTypeImport")
    gui.wait_for_page("importWalletSuccessPage", timeout_ms=30000)
    gui.click("importWalletSuccessOverviewButton")
    gui.wait_for_property("walletBadge", "text", backup_path.stem, timeout_ms=30000)


def open_receive(gui):
    gui.invoke("walletSelectPopup", "close")
    gui.wait_for_property("walletSelectPopup", "visible", False)
    if gui.object_exists("paymentRequestModal"):
        gui.wait_for_property("paymentRequestModal", "visible", False)
    gui.click("receiveTabButton")
    gui.wait_for_page("requestPaymentPage", timeout_ms=10000)
    gui.settle()


def edit_request_field(gui, field, value):
    input_name = {"label": "YourName", "message": "Message", "note": "NoteSelf", "amount": "Amount"}[field]
    if not gui.get_property(f"requestPayment{input_name}Input", "visible"):
        option_name = {"label": "RecipientName", "message": "Message", "amount": "Amount"}[field]
        gui.click("requestPaymentCustomizeButton")
        gui.wait_for_property("requestPaymentFieldsMenu", "opened", True)
        gui.click(f"requestPaymentShow{option_name}")
        gui.invoke("requestPaymentFieldsMenu", "close")
        gui.wait_for_property("requestPaymentFieldsMenu", "visible", False)
    gui.wait_for_property(f"requestPayment{input_name}Input", "visible", True)
    gui.set_text(f"requestPayment{input_name}Input", value)
    gui.invoke(f"requestPayment{input_name}Input", "editingFinished")


def create_request(gui, amount, label, message, note_self=None):
    """Create in the card; the saved request opens in its modal."""
    for field, value in (("amount", amount), ("label", label), ("message", message), ("note", note_self)):
        if value:
            edit_request_field(gui, field, value)
    gui.wait_for_property("requestPaymentGenerateButton", "enabled", True)
    gui.click("requestPaymentGenerateButton")
    gui.wait_for_property("paymentRequestModal", "opened", True)


def request_qr_payload(gui):
    gui.wait_for_property("requestPaymentQRImage", "visible", True)
    return gui.get_property("requestPaymentQRImage", "code")
