#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Encrypted multi-node sending process happy path."""

from decimal import Decimal
from qml_scenarios import run_scenarios
from qml_smoke_lib import (
    create_wallet,
    stop_gui_checked,
    wait_until,
    wallet_scenario,
)
from qml_wallet_test_lib import rpc_call


def send_receive_smoke(harness, checkpoint):
    """Exercise multi-recipient review, broadcast, receipt and Activity navigation."""
    harness.start_source_node(extra_args=["-listen=1", "-bind=127.0.0.1"])
    rpc_call(harness.source_rpc_port, "createwallet", {"wallet_name": "receiver"})
    addresses = [rpc_call(harness.source_rpc_port, "getnewaddress", wallet="receiver") for _ in range(2)]
    harness.start_gui()
    gui = harness.driver
    gui.wait_for_property("walletBadge", "loading", False, timeout_ms=30000)
    create_wallet(harness, "sender", "sending password")
    gui.wait_for_property("walletBadge", "text", "sender", timeout_ms=30000)
    mining_address = rpc_call(harness.gui_rpc_port, "getnewaddress", wallet="sender")
    rpc_call(harness.gui_rpc_port, "generatetoaddress", [101, mining_address])
    stop_gui_checked(harness)
    harness.start_gui()
    gui = harness.driver
    gui.wait_for_property("walletBadge", "text", "sender", timeout_ms=30000)
    assert rpc_call(harness.gui_rpc_port, "getwalletinfo", wallet="sender")["unlocked_until"] == 0
    checkpoint("encrypted wallet restarted locked")
    rpc_call(harness.gui_rpc_port, "addnode", [f"127.0.0.1:{harness.source_p2p_port}", "onetry"])
    wait_until(lambda: rpc_call(harness.source_rpc_port, "getblockcount") == 101, "the recipient node to sync")
    checkpoint("sender funded and recipient node connected")

    gui.click("sendTabButton")
    gui.wait_for_page("sendPage")
    for index, (address, amount) in enumerate(zip(addresses, ("0.01", "0.02"))):
        if index:
            gui.click("sendAddRecipientButton")
        gui.set_text("sendAddressInput", address)
        gui.set_text("sendAmountInput", amount)
    gui.wait_for_property("sendReviewButton", "enabled", True, timeout_ms=30000)
    gui.click("sendReviewButton")
    gui.wait_for_property("reviewPassphrasePopup", "opened", True)
    gui.set_text("reviewPassphraseField", "sending password")
    gui.click("reviewPassphraseConfirmButton")
    gui.wait_for_property("transactionReviewPopup", "opened", True)
    gui.wait_for_page("sendTransactionReviewPage")
    checkpoint("two-recipient payment reviewed")
    gui.click("sendTransactionReviewSendButton")
    gui.wait_for_page("sendCompletePage", timeout_ms=30000)
    txid = gui.get_property("sendCompletePage", "txid")
    assert rpc_call(harness.gui_rpc_port, "getwalletinfo", wallet="sender")["unlocked_until"] == 0
    wait_until(lambda: txid in rpc_call(harness.source_rpc_port, "getrawmempool"), "recipient node to receive the transaction")
    for address, expected in zip(addresses, (Decimal("0.01"), Decimal("0.02"))):
        assert Decimal(str(rpc_call(harness.source_rpc_port, "getreceivedbyaddress", [address, 0], wallet="receiver"))) == expected
    checkpoint("both outputs received on the other node")

    receiver_mining_address = rpc_call(harness.source_rpc_port, "getnewaddress", wallet="receiver")
    rpc_call(harness.source_rpc_port, "generatetoaddress", [1, receiver_mining_address])
    wait_until(lambda: rpc_call(harness.gui_rpc_port, "gettransaction", [txid], wallet="sender")["confirmations"] == 1,
               "the payment confirmation to reach the GUI node")
    gui.click("sendResultViewTransactionButton")
    gui.wait_for_page("activityDetailsPage")
    gui.wait_for_property("activityDetailsPage", "detailsLoading", False, timeout_ms=30000)
    assert gui.get_property("activityDetailsPage", "txid") == txid
    checkpoint("confirmed payment opened in Activity")


if __name__ == "__main__":
    raise SystemExit(run_scenarios('Encrypted multi-node sending process journey', [
        wallet_scenario("encrypted-multinode-send", 6, send_receive_smoke),
    ]))
