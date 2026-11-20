#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Values pasted with a decimal comma, in a locale that writes one."""

import os

from qml_wallet_test_lib import WalletFlowHarness, rpc_call

WALLET_NAME = "decimal_comma"


def paste(gui, object_name, text):
    gui.set_clipboard_text(text)
    gui.invoke(object_name, "forceActiveFocus")
    gui.wait_for_property(object_name, "activeFocus", True)
    gui.invoke(object_name, "selectAll")
    gui.invoke(object_name, "paste")


def run_tests():
    os.environ["LANG"] = "de_DE.UTF-8"
    os.environ["LC_ALL"] = "de_DE.UTF-8"
    harness = WalletFlowHarness("qml_test_decimal_comma", port_offset=160)
    try:
        harness.start_gui()
        gui = harness.driver
        gui.wait_for_property("walletBadge", "loading", False, timeout_ms=20000)
        rpc_call(harness.gui_rpc_port, "createwallet", {"wallet_name": WALLET_NAME})
        gui.wait_for_property("walletBadge", "noWalletLoaded", False, timeout_ms=20000)
        mining_address = rpc_call(harness.gui_rpc_port, "getnewaddress", wallet=WALLET_NAME)
        destination = rpc_call(harness.gui_rpc_port, "getnewaddress", wallet=WALLET_NAME)
        rpc_call(harness.gui_rpc_port, "generatetoaddress", [103, mining_address])
        gui.click("sendTabButton")
        gui.wait_for_page("sendPage")
        gui.set_text("sendAddressInput", destination)

        print("Test 1: a pasted amount replaces a typed one")
        gui.invoke("sendAmountInput", "forceActiveFocus")
        gui.type_text("sendAmountInput", "987")
        paste(gui, "sendAmountInput", "1,23456789")
        gui.wait_for_property("sendAmountInput", "text", "1,23456789")
        gui.invoke("sendAddressInput", "forceActiveFocus")
        gui.wait_for_property("sendAmountInput", "text", "1.23456789")
        print("  PASSED")

        print("Test 2: a pasted fee rate is paid at its decimal value")
        gui.click("feeSelectionPickerButton")
        gui.click("feeSelectionOption3")
        gui.set_text("feeSelectionCustomRateInput", "1.5")
        gui.wait_for_property("sendPage", "wallet.feeEstimatePending", False, timeout_ms=20000)
        gui.wait_for_property("sendPage", "wallet.estimatedFee", lambda v: v != "", timeout_ms=20000)
        period_fee = gui.get_property("sendPage", "wallet.estimatedFee")
        gui.set_text("feeSelectionCustomRateInput", "")
        paste(gui, "feeSelectionCustomRateInput", "1,5")
        gui.wait_for_property("feeSelectionCustomRateInput", "text", "1,5")
        gui.wait_for_property("sendPage", "wallet.customFeeRateValid", True)
        gui.wait_for_property("sendPage", "wallet.feeEstimatePending", False, timeout_ms=20000)
        gui.wait_for_property("sendPage", "wallet.estimatedFee", period_fee, timeout_ms=20000)
        print("  PASSED")
    finally:
        harness.stop()

    print("\n" + "=" * 50)
    print("All tests PASSED")
    print("=" * 50)


if __name__ == "__main__":
    run_tests()
