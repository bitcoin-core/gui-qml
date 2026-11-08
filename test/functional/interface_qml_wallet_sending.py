#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Encrypted multi-node sending and PSBT/replacement process happy paths."""

import base64
from decimal import Decimal
from pathlib import Path
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


def psbt_smoke(harness, checkpoint):
    """Exercise file import, review and broadcast for a fully signed foreign PSBT."""
    harness.start_gui()
    gui = harness.driver
    gui.wait_for_property("walletBadge", "loading", False, timeout_ms=30000)
    for name in ("signer", "recipient", "reviewer"):
        rpc_call(harness.gui_rpc_port, "createwallet", {"wallet_name": name})
    gui.wait_for_property("walletBadge", "text", "reviewer", timeout_ms=30000)
    addresses = [rpc_call(harness.gui_rpc_port, "getnewaddress", wallet="recipient") for _ in range(2)]
    mining_address = rpc_call(harness.gui_rpc_port, "getnewaddress", wallet="signer")
    rpc_call(harness.gui_rpc_port, "generatetoaddress", [101, mining_address])
    funded = rpc_call(harness.gui_rpc_port, "walletcreatefundedpsbt", [
        [], [{addresses[0]: 1}, {addresses[1]: 0.5}], 0, {"fee_rate": 2}, True,
    ], wallet="signer")
    signed = rpc_call(harness.gui_rpc_port, "walletprocesspsbt", [funded["psbt"], True, "ALL", True, True], wallet="signer")
    assert signed["complete"]
    finalized = rpc_call(harness.gui_rpc_port, "finalizepsbt", [signed["psbt"]])
    txid = rpc_call(harness.gui_rpc_port, "decoderawtransaction", [finalized["hex"]])["txid"]
    path = Path(harness.tmpdir) / "signed foreign.psbt"
    path.write_bytes(base64.b64decode(signed["psbt"]))
    checkpoint("another wallet signed the two-recipient PSBT")

    gui.click("sendTabButton")
    gui.wait_for_page("sendPage")
    gui.click("sendOptionsButton")
    gui.wait_for_property("sendOptionsPopup", "opened", True)
    gui.set_text("psbtImportPathField", str(path))
    gui.click("sendImportPsbtFromFileButton")
    gui.wait_for_property("transactionReviewPopup", "opened", True, timeout_ms=30000)
    gui.wait_for_property("sendTransactionReviewRecipient1", "visible", True)
    gui.wait_for_property("sendTransactionReviewSendButton", "enabled", True)
    checkpoint("signed PSBT imported and reviewed")
    gui.click("sendTransactionReviewSendButton")
    gui.wait_for_page("sendCompletePage", timeout_ms=30000)
    assert gui.get_property("sendCompletePage", "txid") == txid
    assert txid in rpc_call(harness.gui_rpc_port, "getrawmempool")
    for address, amount in zip(addresses, (Decimal("1"), Decimal("0.5"))):
        wait_until(
            lambda: Decimal(str(rpc_call(harness.gui_rpc_port, "getreceivedbyaddress", [address, 0], wallet="recipient"))) == amount,
            f"the recipient wallet to record the PSBT output at {address}",
        )
    gui.click("sendResultDoneButton")
    gui.wait_for_property("transactionReviewPopup", "visible", False)
    gui.wait_for_page("sendPage")
    checkpoint("both PSBT outputs broadcast and returned to Send")


def replacement_smoke(harness, checkpoint):
    """Replace an encrypted wallet's payment through Activity and confirm it."""
    harness.start_gui()
    gui = harness.driver
    gui.wait_for_property("walletBadge", "loading", False, timeout_ms=30000)
    rpc_call(harness.gui_rpc_port, "createwallet", {"wallet_name": "receiver"})
    address = rpc_call(harness.gui_rpc_port, "getnewaddress", wallet="receiver")
    rpc_call(harness.gui_rpc_port, "createwallet", {"wallet_name": "sender", "passphrase": "replacement password"})
    gui.wait_for_property("walletBadge", "text", "sender", timeout_ms=30000)
    mining_address = rpc_call(harness.gui_rpc_port, "getnewaddress", wallet="sender")
    rpc_call(harness.gui_rpc_port, "generatetoaddress", [101, mining_address])
    rpc_call(harness.gui_rpc_port, "walletpassphrase", ["replacement password", 60], wallet="sender")
    txid = rpc_call(harness.gui_rpc_port, "sendtoaddress", [address, 1], wallet="sender")
    rpc_call(harness.gui_rpc_port, "walletlock", wallet="sender")
    gui.click("activityTabButton")
    gui.wait_for_property("activityTabButton", "checked", True)
    gui.set_text("activitySearchField", txid)
    gui.wait_for_property("activityFilterProxyModel", "count", 1, timeout_ms=30000)
    gui.click_list_item("activityListView", 0, "activityRowOpenButton")
    gui.wait_for_page("activityDetailsPage")
    gui.wait_for_property("activityDetailsPage", "detailsLoading", False, timeout_ms=30000)
    gui.click("speedUpBannerPrimaryButton")
    gui.wait_for_property("speedUpOverlay", "opened", True)
    gui.wait_for_property("updateTransactionButton", "enabled", True, timeout_ms=30000)
    gui.click("updateTransactionButton")
    gui.wait_for_property("speedUpPassphrasePopup", "opened", True)
    gui.set_text("speedUpPassphraseField", "wrong password")
    gui.click("speedUpPassphraseConfirmButton")
    gui.wait_for_property("speedUpPassphraseErrorText", "text", "The wallet password you entered was incorrect.")
    assert rpc_call(harness.gui_rpc_port, "getwalletinfo", wallet="sender")["unlocked_until"] == 0
    gui.set_text("speedUpPassphraseField", "replacement password")
    gui.click("speedUpPassphraseConfirmButton")
    gui.wait_for_property("speedUpPassphrasePopup", "visible", False, timeout_ms=30000)
    wait_until(lambda: txid not in rpc_call(harness.gui_rpc_port, "getrawmempool"), "the replacement to enter the mempool")
    pool = rpc_call(harness.gui_rpc_port, "getrawmempool")
    assert len(pool) == 1 and pool[0] != txid
    replacement = pool[0]
    assert replacement in rpc_call(harness.gui_rpc_port, "gettransaction", [txid], wallet="sender")["walletconflicts"]
    assert rpc_call(harness.gui_rpc_port, "getwalletinfo", wallet="sender")["unlocked_until"] == 0
    checkpoint("encrypted replacement broadcast and wallet relocked")
    rpc_call(harness.gui_rpc_port, "generatetoaddress", [1, mining_address])
    wait_until(lambda: rpc_call(harness.gui_rpc_port, "gettransaction", [replacement], wallet="sender")["confirmations"] >= 1,
               "the replacement to confirm")
    checkpoint("replacement confirmed")


if __name__ == "__main__":
    raise SystemExit(run_scenarios('Sending, PSBT and replacement process journeys', [
        wallet_scenario("encrypted-multinode-send", 6, send_receive_smoke),
        wallet_scenario("signed-foreign-psbt", 7, psbt_smoke),
        wallet_scenario("encrypted-replacement", 7, replacement_smoke),
    ]))
