#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Sign, browse addresses, receive a payment and reopen its receipt after restart."""

from pathlib import Path
from qml_scenarios import run_scenarios
from qml_smoke_lib import (
    create_request,
    open_receive,
    open_wallet_settings,
    request_qr_payload,
    stop_gui_checked,
    wallet_scenario,
)
from qml_wallet_test_lib import (
    open_wallet_selector,
    rpc_call,
)
from urllib.parse import urlparse


def receive_smoke(harness, checkpoint):
    """Exercise a real payment request and its persisted receipt."""
    harness.start_gui()
    gui = harness.driver
    gui.wait_for_property("walletBadge", "loading", False, timeout_ms=30000)
    rpc_call(harness.gui_rpc_port, "createwallet", {"wallet_name": "payer"})
    mining_address = rpc_call(harness.gui_rpc_port, "getnewaddress", wallet="payer")
    rpc_call(harness.gui_rpc_port, "generatetoaddress", [101, mining_address])
    rpc_call(harness.gui_rpc_port, "createwallet", {"wallet_name": "receiver", "passphrase": "activity password"})
    gui.wait_for_property("walletBadge", "text", "receiver", timeout_ms=30000)
    address = rpc_call(harness.gui_rpc_port, "getnewaddress", ["Smoke signing address", "legacy"], wallet="receiver")
    open_wallet_settings(gui)
    gui.click("walletSignVerifyMessageRow")
    gui.wait_for_page("signVerifyMessagePage")
    gui.set_text("signMessageAddressField", address)
    gui.set_text("signMessageMessageField", "Smoke test message")
    gui.click("signMessageButton")
    gui.wait_for_property("signMessagePassphrasePopup", "opened", True)
    gui.set_text("signMessagePassphraseField", "activity password")
    gui.click("signMessagePassphraseConfirmButton")
    gui.wait_for_property("signMessageSignatureOutput", "visible", True)
    signature = gui.get_text("signMessageSignatureText")
    assert signature
    gui.click("verifyMessageTab")
    gui.set_text("verifyMessageAddressField", address)
    gui.set_text("verifyMessageMessageField", "Smoke test message")
    gui.set_text("verifyMessageSignatureField", signature)
    gui.click("verifyMessageButton")
    gui.wait_for_property("verifyMessageResultText", "text", "Message verified successfully.")
    checkpoint("message signed and verified")

    gui.click("signVerifyMessageBackButton")
    gui.wait_for_page("walletSettingsPage")
    gui.click("walletAddressesRow")
    gui.wait_for_page("addressListPage")
    gui.wait_for_property("addressListView", "count", lambda count: count > 0)
    gui.click_list_item("addressListView", 0, "addressRowDetailsButton")
    gui.wait_for_property("addressDetailsPopup", "opened", True)
    assert gui.get_property("addressDetails", "address") == address
    gui.invoke("addressDetailsPopup", "close")
    checkpoint("wallet address opened")

    open_receive(gui)
    create_request(gui, "0.0001", "Alice", "pizza", note_self="Private lunch note")
    address = urlparse(request_qr_payload(gui)).path
    assert rpc_call(harness.gui_rpc_port, "getaddressinfo", [address], wallet="receiver")["ismine"]
    checkpoint("payment request created")

    qr_path = Path(harness.tmpdir) / "payment-qr.png"
    gui.click("paymentRequestMoreButton")
    gui.wait_for_property("paymentRequestMoreMenu", "opened", True)
    gui.click("requestPaymentSaveQRMenuButton")
    gui.wait_for_property("requestPaymentSaveQRDialog", "visible", True)
    gui.set_property("requestPaymentSaveQRDialog", "selectedFile", qr_path.as_uri())
    gui.invoke("requestPaymentSaveQRDialog", "accepted")
    gui.invoke("requestPaymentSaveQRDialog", "close")
    assert qr_path.read_bytes().startswith(bytes.fromhex("89504e470d0a1a0a"))
    checkpoint("payment QR exported")

    txid = rpc_call(harness.gui_rpc_port, "sendtoaddress", {
        "address": address, "amount": 0.00001, "fee_rate": 1,
    }, wallet="payer")
    gui.wait_for_property("paymentRequestStatus", "text", "Payment received", timeout_ms=30000)
    gui.click("paymentRequestModalClose")
    gui.wait_for_property("paymentRequestModal", "visible", False)
    checkpoint("request received a payment")

    gui.click("activityTabButton")
    gui.wait_for_property("activityMoreButton", "visible", True)
    assert gui.get_property("activityExportPathField", "text") == ""
    gui.click("activityMoreButton")
    gui.wait_for_property("activityMoreMenu", "opened", True)
    gui.click("activityExportButton")
    gui.wait_for_property("activityExportDialog", "visible", True, timeout_ms=20000)
    gui.invoke("activityExportDialog", "close")
    gui.wait_for_property("activityExportDialog", "visible", False)
    assert harness.gui_process.poll() is None
    checkpoint("native or fallback export dialog opened and closed")

    stop_gui_checked(harness)
    harness.start_gui()
    gui = harness.driver
    gui.wait_for_property("walletBadge", "loading", False, timeout_ms=30000)
    open_wallet_selector(gui)
    gui.click("walletSelectItem_receiver")
    gui.wait_for_property("walletBadge", "text", "receiver", timeout_ms=30000)
    gui.click("activityTabButton")
    gui.set_text("activitySearchField", txid)
    gui.wait_for_property(f"activityItem_{txid}", "visible", True, timeout_ms=30000)
    gui.click_list_item("activityListView", 0, "activityRowOpenButton")
    gui.wait_for_page("activityDetailsPage")
    gui.wait_for_property("activityDetailsPage", "detailsLoading", False, timeout_ms=30000)
    gui.wait_for_property("transactionFlowRequest_1", "visible", True)
    gui.click("transactionFlowRequest_1")
    gui.wait_for_property("transactionPaymentRequestModal", "opened", True)
    gui.wait_for_property("paymentRequestStatus", "text", "Payment received")
    gui.wait_for_property("requestPaymentNoteRow", "value", "Private lunch note")
    checkpoint("receipt reopened from Activity after restart")

if __name__ == "__main__":
    raise SystemExit(run_scenarios('Wallet Activity and native export process journey', [
        wallet_scenario("request-receipt-restart-and-dialog", 5, receive_smoke),
    ]))
