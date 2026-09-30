#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Send V3 accordion, payment request, coin selection, and settings integration."""
import os
from qml_wallet_test_lib import WalletFlowHarness, rpc_call


def run_test():
    harness = WalletFlowHarness("qml_test_send_create", port_offset=90)
    artifacts = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "build", "test-artifacts", "send-v3"))
    os.makedirs(artifacts, exist_ok=True)
    try:
        harness.start_gui()
        gui = harness.driver
        gui.wait_for_property("walletBadge", "loading", False, timeout_ms=20000)
        rpc_call(harness.gui_rpc_port, "createwallet", {"wallet_name": "send_create"})
        gui.wait_for_property("walletBadge", "noWalletLoaded", False, timeout_ms=20000)
        addresses = [rpc_call(harness.gui_rpc_port, "getnewaddress", [note], wallet="send_create")
                     for note in ["Rent · September", "Savings", "Donation"]]
        rpc_call(harness.gui_rpc_port, "generatetoaddress", [103, addresses[0]])
        gui.set_property("appWindow", "width", 1180)
        gui.set_property("appWindow", "height", 1350)
        gui.click("sendTabButton")
        gui.wait_for_page("sendPage")
        missing_recipient_message = "Enter a valid recipient address and amount to estimate the fee"
        gui.wait_for_property("sendNetworkFeeSectionFooter", "text", missing_recipient_message)
        assert gui.get_property("feeSelectionEstimateLabel", "value") == "—"
        gui.set_text("sendAmountInput", "0.018")
        gui.wait_for_property("sendNetworkFeeSectionFooter", "text", missing_recipient_message)
        assert gui.get_property("feeSelectionEstimateLabel", "value") == "—"
        gui.set_text("sendAmountInput", "")
        gui.save_screenshot(os.path.join(artifacts, "01-empty.png"))
        gui.set_text("sendNoteInput", "My private note")
        uri = f"bitcoin:{addresses[1]}?amount=0.018&label=Alice&message=September%20rent"
        gui.set_clipboard_text(uri)
        gui.wait_for_property("clipboardUriPasteButton", "visible", True)
        gui.click("clipboardUriPasteButton")
        gui.wait_for_property("sendPaymentRequestPayTo", "value", "Alice")
        gui.wait_for_property("sendPaymentRequestMessageText", "value", "September rent")
        assert gui.get_text("sendNoteInput") == "My private note"
        gui.wait_for_property("sendReviewButton", "enabled", True, timeout_ms=20000)
        gui.wait_for_property("sendNetworkFeeSectionFooter", "visible", False)
        assert gui.get_property("feeSelectionEstimateLabel", "value") != "—"
        gui.save_screenshot(os.path.join(artifacts, "02-request.png"))
        gui.click("sendAddRecipientButton")
        gui.wait_for_property("sendRecipientCard_0", "expanded", False)
        gui.set_text("sendAddressInput", addresses[2])
        gui.set_text("sendAmountInput", "0.025")
        gui.set_text("sendNoteInput", "Donation")
        gui.click("feeSelectionPickerButton")
        gui.click("feeSelectionOption3")
        gui.set_text("feeSelectionCustomRateInput", "4.2")
        gui.wait_for_property("sendReviewButton", "enabled", True, timeout_ms=20000)
        gui.save_screenshot(os.path.join(artifacts, "03-multiple-custom.png"))
        gui.click("sendEditRecipient_0")
        gui.wait_for_property("sendPaymentRequestPayTo", "value", "Alice")
        assert gui.get_text("sendNoteInput") == "My private note"
        gui.click("sendSelectInputsButton")
        gui.wait_for_property("coinSelectionPopup", "opened", True)
        assert gui.get_property("coinSelectionDoneButton", "enabled") is False
        gui.save_screenshot(os.path.join(artifacts, "04-coins-empty.png"))
        gui.click_list_item("coinSelectionListView", 0, "coinSelectionCheckbox")
        gui.wait_for_property("coinSelectionDoneButton", "enabled", True, timeout_ms=20000)
        gui.set_text("coinBrowserSearch", "no matching note")
        gui.wait_for_property("coinSelectionListView", "count", 0)
        assert gui.get_property("coinSelectionDoneButton", "enabled") is True
        gui.click("coinSelectionCancelButton")
        gui.wait_for_property("coinSelectionPopup", "visible", False)
        gui.click("sendSelectInputsButton")
        gui.wait_for_property("coinSelectionPopup", "opened", True)
        assert gui.get_text("coinSelectionTotalSelectedText") == "0 inputs selected"
        gui.set_text("coinBrowserSearch", "")
        gui.click_list_item("coinSelectionListView", 0, "coinSelectionCheckbox")
        gui.wait_for_property("coinSelectionDoneButton", "enabled", True, timeout_ms=20000)
        gui.save_screenshot(os.path.join(artifacts, "05-coins-covered.png"))
        gui.click("coinSelectionDoneButton")
        gui.wait_for_property("coinSelectionPopup", "visible", False)
        gui.wait_for_property("sendInputsSelectedText", "text", "1 input selected")
        gui.click("desktopWalletSettingsTabButton")
        gui.click("settingsSidebar_wallet")
        gui.wait_for_page("walletSettingsPage")
        gui.click("walletCoinsRow")
        gui.wait_for_page("walletCoinsPage")
        gui.save_screenshot(os.path.join(artifacts, "06-settings-coins.png"))
        gui.click("walletCoinsSelectionButton")
        gui.click_list_item("coinSelectionListView", 0, "coinSelectionCheckbox")
        gui.wait_for_property("coinsLockButton", "enabled", True)
        gui.click("coinsLockButton")
        assert len(rpc_call(harness.gui_rpc_port, "listlockunspent", wallet="send_create")) == 1
        gui.click_list_item("coinSelectionListView", 0, "coinSelectionCheckbox")
        gui.wait_for_property("coinsUnlockButton", "enabled", True)
        gui.click("coinsUnlockButton")
        assert rpc_call(harness.gui_rpc_port, "listlockunspent", wallet="send_create") == []
        gui.set_property("appWindow", "width", 800)
        gui.set_property("appWindow", "height", 665)
        gui.save_screenshot(os.path.join(artifacts, "07-settings-compact.png"))
        gui.click("settingsSidebar_display")
        gui.click("displayThemePickerOption_0")
        gui.click("sendTabButton")
        gui.click("sendEditRecipient_0")
        gui.set_property("appWindow", "width", 1180)
        gui.set_property("appWindow", "height", 1350)
        gui.wait_for_property("sendReviewButton", "enabled", True, timeout_ms=20000)
        gui.save_screenshot(os.path.join(artifacts, "08-send-light.png"))
        print("Send V3 integration passed; screenshots:", artifacts)
    finally:
        harness.stop()


if __name__ == "__main__":
    run_test()
