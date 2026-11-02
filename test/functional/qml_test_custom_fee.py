#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Explicit custom rates override the wallet minimum but respect the relay minimum."""

from decimal import Decimal
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "bitcoin", "test", "functional"))

from qml_wallet_test_lib import WalletFlowHarness, rpc_call
from test_framework.util import assert_fee_amount


def run_case(wallet_minimum, relay_minimum, rate, below_minimum=None):
    harness = WalletFlowHarness("qml_test_custom_fee", port_offset=145)
    try:
        harness.start_gui(extra_args=[f"-mintxfee={wallet_minimum}", f"-minrelaytxfee={relay_minimum}"])
        gui = harness.driver
        gui.wait_for_property("walletBadge", "loading", False, timeout_ms=20000)
        rpc_call(harness.gui_rpc_port, "createwallet", {"wallet_name": "custom_fee"})
        gui.wait_for_property("walletBadge", "noWalletLoaded", False, timeout_ms=20000)
        mining_address = rpc_call(harness.gui_rpc_port, "getnewaddress", wallet="custom_fee")
        destination = rpc_call(harness.gui_rpc_port, "getnewaddress", wallet="custom_fee")
        rpc_call(harness.gui_rpc_port, "generatetoaddress", [103, mining_address])
        gui.click("sendTabButton")
        gui.wait_for_page("sendPage")
        gui.set_text("sendAddressInput", destination)
        gui.set_text("sendAmountInput", "0.01")
        gui.click("feeSelectionPickerButton")
        gui.click("feeSelectionOption3")

        if below_minimum is not None:
            gui.set_text("feeSelectionCustomRateInput", below_minimum)
            gui.wait_for_property("sendPage", "wallet.customFeeRateValid", False)
            gui.wait_for_property("sendReviewButton", "enabled", False)
            gui.wait_for_property("sendPage", "wallet.feeEstimatePending", False, timeout_ms=20000)
            assert gui.get_property("sendPage", "wallet.estimatedFee") == ""
            assert gui.get_property("feeSelectionEstimateLabel", "value") == "—"
            assert gui.get_property("feeSelectionCustomRateRow", "errorText") == "Enter a fee rate of at least 1 sat/vB."
            # Preparation must also reject this rate if called without the UI guard.
            assert gui.invoke_property_object("sendPage", "wallet", "prepareTransaction") is False
            assert rpc_call(harness.gui_rpc_port, "getrawmempool") == []

        gui.set_text("feeSelectionCustomRateInput", rate)
        gui.wait_for_property("sendReviewButton", "enabled", True, timeout_ms=20000)
        gui.click("sendReviewButton")
        gui.wait_for_page("sendTransactionReviewPage", timeout_ms=20000)
        gui.click("sendTransactionReviewSendButton")
        gui.wait_for_page("sendCompletePage", timeout_ms=20000)
        txid = gui.get_property("sendCompletePage", "txid")
        assert rpc_call(harness.gui_rpc_port, "getrawmempool") == [txid]
        tx = rpc_call(harness.gui_rpc_port, "gettransaction", [txid], wallet="custom_fee")
        decoded = rpc_call(harness.gui_rpc_port, "decoderawtransaction", [tx["hex"]])
        # Core budgets the fee using the maximum signed size. A shorter ECDSA
        # signature can reduce the final vsize, so use Core's bounded assertion.
        assert_fee_amount(-Decimal(str(tx["fee"])), decoded["vsize"], Decimal(rate) / 100000)
        print(f"Custom {rate} sat/vB broadcast with relay minimum {relay_minimum}: passed")
    finally:
        harness.stop()


if __name__ == "__main__":
    run_case("0.00001000", "0.00000100", "0.1")
    run_case("0.00000100", "0.00001000", "1", below_minimum="0.1")
