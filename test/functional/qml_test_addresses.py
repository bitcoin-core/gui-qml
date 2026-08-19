#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""End-to-end GUI test for wallet address list receive integration."""

import sys
import time

from qml_driver import QmlDriverError
from qml_test_harness import complete_onboarding, dump_qml_tree
from qml_wallet_test_lib import WalletFlowHarness, rpc_call, wait_for_rpc


WALLET_NAME = "Alice"
ADDRESS_LABEL = "invoice 1024"


def wait_for_text(gui, object_name, expected, timeout_ms=10000):
    deadline = time.time() + timeout_ms / 1000
    last_text = ""
    while time.time() < deadline:
        last_text = gui.get_text(object_name)
        if last_text == expected:
            return
        time.sleep(0.1)
    raise AssertionError(f"Expected {object_name} text {expected!r}, got {last_text!r}")


def create_wallet_through_gui(harness, gui):
    complete_onboarding(gui)
    if gui.object_exists("createWalletWizard"):
        gui.click("createWalletWizardExitButton")
    gui.wait_for_property("walletBadge", "loading", False, timeout_ms=30000)
    wait_for_rpc(harness.gui_rpc_port, timeout=30)
    gui.click("walletBadge")
    try:
        gui.wait_for_property("walletTypeRegular", "visible", True, timeout_ms=3000)
    except QmlDriverError:
        gui.wait_for_property("walletSelectPopup", "opened", True, timeout_ms=5000)
        gui.click("walletSelectAddWalletButton")
        gui.wait_for_property("walletTypeRegular", "visible", True, timeout_ms=5000)
    gui.settle()
    gui.click("walletTypeRegular")
    gui.wait_for_property("createWalletIntroStartButton", "visible", True, timeout_ms=5000)
    gui.click("createWalletIntroStartButton")
    gui.settle()
    gui.set_text("createWalletNameInput", WALLET_NAME)
    gui.click("createWalletNameContinueButton")
    gui.settle()
    gui.click("createWalletPasswordSkipButton")
    gui.wait_for_property("createWalletConfirmNextButton", "visible", True, timeout_ms=20000)
    gui.click("createWalletConfirmNextButton")
    gui.wait_for_property("createWalletBackupDoneButton", "visible", True, timeout_ms=10000)
    gui.click("createWalletBackupDoneButton")
    gui.settle()
    try:
        wizard_visible = gui.get_property("createWalletWizard", "visible")
    except QmlDriverError as err:
        if "Object not found: createWalletWizard" not in str(err):
            raise
        wizard_visible = False
    if wizard_visible:
        gui.click("typeSelectorCancelButton")
        gui.settle()
    gui.wait_for_property("walletBadge", "text", WALLET_NAME, timeout_ms=20000)


def fund_wallet_for_send_context(harness):
    wait_for_rpc(harness.gui_rpc_port, timeout=30)
    mining_address = rpc_call(
        harness.gui_rpc_port,
        "getnewaddress",
        ["mining rewards"],
        wallet=WALLET_NAME,
    )
    rpc_call(harness.gui_rpc_port, "generatetoaddress", [101, mining_address])


def create_labeled_receive_address(harness):
    return rpc_call(
        harness.gui_rpc_port,
        "getnewaddress",
        [ADDRESS_LABEL],
        wallet=WALLET_NAME,
    )


def open_address_list_from_settings(gui):
    gui.click("desktopWalletSettingsTabButton")
    gui.wait_for_property("desktopWalletSettingsTabButton", "checked", True, timeout_ms=5000)
    gui.settle()
    gui.wait_for_property("settingsSidebar_wallet", "visible", True, timeout_ms=5000)
    gui.click("settingsSidebar_wallet")
    gui.wait_for_property("settingsv2WalletSettingsPage", "visible", True, timeout_ms=5000)
    gui.click("settingsv2WalletAddressesRow")
    gui.wait_for_property("addressListPage", "visible", True, timeout_ms=10000)


def create_payment_request_from_first_unused_address(gui, expected_address):
    gui.wait_for_property("addressListView", "count", lambda count: count > 0, timeout_ms=10000)
    row_count = gui.get_property("addressListView", "count")
    for row in range(row_count):
        if gui.get_list_item_property("addressListView", row, "address") == expected_address:
            gui.click_list_item("addressListView", row, "addressRowMenuButton")
            break
    else:
        raise AssertionError(f"Expected address {expected_address!r} in address list")

    gui.wait_for_property("addressRowCreatePaymentRequestButton", "visible", True, timeout_ms=5000)
    gui.click("addressRowCreatePaymentRequestButton")
    gui.settle()
    wait_for_text(gui, "requestPaymentLabelInput", ADDRESS_LABEL)
    address = gui.get_property("requestPaymentAddressText", "address")
    assert address == expected_address, (
        f"Expected receive view address {expected_address!r}, got {address!r}"
    )


def run_test():
    harness = WalletFlowHarness("qml_addresses", port_offset=70)
    try:
        harness.start_gui()
        gui = harness.driver
        create_wallet_through_gui(harness, gui)
        fund_wallet_for_send_context(harness)
        new_address = create_labeled_receive_address(harness)
        open_address_list_from_settings(gui)
        create_payment_request_from_first_unused_address(gui, new_address)
        print("Address list receive integration flow passed.")
        return 0
    except Exception as err:  # noqa: BLE001 - preserve GUI context on failures
        print(f"\nFAILED [qml_addresses]: {err}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        if harness.driver:
            dump_qml_tree(harness.driver)
        gui_output = harness.process_output(harness.gui_process)
        if gui_output:
            print("\n--- GUI process output ---", file=sys.stderr)
            print(gui_output, file=sys.stderr)
        return 1
    finally:
        harness.stop()


if __name__ == "__main__":
    sys.exit(run_test())
