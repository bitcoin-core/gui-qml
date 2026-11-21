#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Sign, browse addresses, receive a payment and reopen its receipt after restart."""

import json
import os
from pathlib import Path
from qml_process_checks import check_gui_exit
from qml_scenarios import run_scenarios
from qml_smoke_lib import (
    create_request,
    create_wallet,
    open_receive,
    open_wallet_settings,
    request_qr_payload,
    select_wallet,
    stop_gui_checked,
    wait_until,
    wallet_scenario,
)
from qml_wallet_test_lib import (
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
    select_wallet(gui, "receiver")
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
    gui.wait_for_property("paymentRequestModal/savedRequestExportQrButton", "enabled", True)
    original_request = gui.get_property("paymentRequestModal", "openedEditor.requestId")
    gui.click("paymentRequestModal/savedRequestExportQrButton")
    gui.wait_for_property("paymentRequestModal/savedRequestPngDialog", "visible", True)
    original_export = gui.get_property("paymentRequestModal/savedRequestPngDialog", "contextObject.operationId")
    assert original_export
    assert gui.invoke_property_object("paymentRequestModal/savedRequestPngDialog", "contextObject", "choose", [qr_path.as_uri()]) is True

    def original_png_written():
        assert gui.get_property("paymentRequestModal/savedRequestPngDialog", "contextObject.operationId") == original_export
        assert gui.get_property("paymentRequestModal", "openedEditor.requestId") == original_request
        phase = gui.get_enum_property("paymentRequestModal/savedRequestPngDialog", "contextObject.phase")
        assert phase["key"] not in ("Failed", "OutcomeUnknown", "Cancelled"), phase
        return phase["key"] == "Completed"

    wait_until(original_png_written, "the original QR export's confirmed write")
    assert gui.get_property("paymentRequestModal/savedRequestPngDialog", "contextObject.finalPath") == str(qr_path)
    assert gui.get_property("paymentRequestModal/savedRequestPngDialog", "contextObject.errorText") in (None, "")
    gui.invoke("paymentRequestModal/savedRequestPngDialog", "close")
    gui.wait_for_property("paymentRequestModal/savedRequestPngDialog", "visible", False)
    assert qr_path.is_file() and qr_path.read_bytes().startswith(bytes.fromhex("89504e470d0a1a0a"))
    checkpoint("payment QR exported")

    txid = rpc_call(harness.gui_rpc_port, "sendtoaddress", {
        "address": address, "amount": 0.00001, "fee_rate": 1,
    }, wallet="payer")
    gui.wait_for_property("paymentRequestModal", "openedEditor.receipts.fresh", True, timeout_ms=30000)
    gui.wait_for_property("paymentRequestModal/savedRequestReceiptStatus", "text", "Payment received", timeout_ms=30000)
    gui.click("paymentRequestModal/savedRequestCloseButton")
    gui.wait_for_property("paymentRequestModal", "visible", False)
    checkpoint("request received a payment")

    gui.click("activityTabButton")
    gui.wait_for_property("activityMoreButton", "visible", True)
    assert gui.get_property("activityExportPathField", "text") == ""
    gui.click("activityMoreButton")
    gui.wait_for_property("activityMoreMenu", "opened", True)
    # Export is disabled while Activity refreshes after the received payment.
    gui.wait_for_property("activityExportButton", "enabled", True, timeout_ms=30000)
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
    select_wallet(gui, "receiver")
    gui.click("activityTabButton")
    gui.set_text("activitySearchField", txid)
    gui.wait_for_property(f"activityItem_{txid}", "visible", True, timeout_ms=30000)
    gui.click_list_item("activityListView", 0, "activityRowOpenButton")
    gui.wait_for_page("activityDetailsPage")
    gui.wait_for_property("activityDetailsPage", "detailsLoading", False, timeout_ms=30000)
    gui.wait_for_property(f"transactionFlowRequest_{original_request}", "visible", True)
    gui.click(f"transactionFlowRequest_{original_request}")
    gui.wait_for_property("transactionPaymentRequestModal", "opened", True)
    gui.wait_for_property("transactionPaymentRequestModal", "owned.openedEditor.receipts.fresh", True)
    gui.wait_for_property("transactionPaymentRequestModal/savedRequestReceiptStatus", "text", "Payment received")
    assert gui.get_property("transactionPaymentRequestModal", "owned.openedEditor.requestId") == original_request
    assert gui.get_property("transactionPaymentRequestModal", "owned.openedEditor.address") == address
    assert gui.get_property("transactionPaymentRequestModal", "owned.openedEditor.privateNote.text") == "Private lunch note"
    checkpoint("receipt reopened from Activity after restart")

def typed_receipt_restart(harness, checkpoint):
    """Keep current credit and durable address-use distinct across real restart."""
    def checked_stop(phase):
        process = harness.gui_process
        harness.stop_gui()
        assert process is not None
        stdout, stderr = process.communicate()
        if os.getenv("QML_TEST_ARTIFACTS"):
            output = Path(os.environ["QML_TEST_ARTIFACTS"]) / "typed-current-receipt-reorg-restart" / "processes"
            output.mkdir(parents=True, exist_ok=True)
            (output / f"{phase}.stdout").write_bytes(stdout)
            (output / f"{phase}.stderr").write_bytes(stderr)
            (output / f"{phase}.json").write_text(json.dumps({
                "pid": process.pid, "command": process.args,
                "exit": process.returncode, "socket_path": harness.socket_path,
            }, indent=2) + "\n", encoding="utf-8")
        check_gui_exit(process.returncode, stderr.decode("utf-8", errors="replace"), socket_path=harness.socket_path)

    harness.start_gui()
    create_wallet(harness, "receipt-restart", "receipt password")
    gui = harness.driver
    gui.click("receiveTabButton")
    gui.wait_for_property("receiveAmountInput", "visible", True, timeout_ms=30000)
    gui.set_text("receiveAmountInput", "1.0")
    gui.invoke("receiveAmountInput", "editingFinished")
    gui.set_text("receiveMessageInput", "Original public invoice")
    gui.set_text("receivePrivateNoteInput", "Original private receipt note")
    gui.wait_for_property("receiveSaveRequestButton", "enabled", True)
    gui.click("receiveSaveRequestButton")
    gui.wait_for_property("paymentRequestModal", "opened", True, timeout_ms=30000)
    request_id = gui.get_property("paymentRequestModal", "openedEditor.requestId")
    address = gui.get_property("paymentRequestModal", "openedEditor.address")
    assert request_id and address
    gui.wait_for_property("paymentRequestModal", "openedEditor.receipts.fresh", True)
    assert gui.get_property("paymentRequestModal", "openedEditor.receipts.receivedAmountSatoshi") == 0
    checkpoint("original typed request is awaiting a current receipt")

    block = rpc_call(harness.gui_rpc_port, "generatetoaddress", [1, address])[0]
    rpc_call(harness.gui_rpc_port, "syncwithvalidationinterfacequeue")
    gui.wait_for_property("paymentRequestModal", "openedEditor.receipts.receivedAmountSatoshi", 5_000_000_000, timeout_ms=30000)
    gui.wait_for_property("paymentRequestModal", "openedEditor.receipts.fresh", True)
    gui.wait_for_property("paymentRequestModal", "openedEditor.historicallyUsed", True)
    gui.wait_for_property("savedRequestReceiptStatus", "text", "Payment received")
    gui.wait_for_property("savedRequestCopyUriButton", "enabled", False)
    rpc_call(harness.gui_rpc_port, "invalidateblock", [block])
    rpc_call(harness.gui_rpc_port, "syncwithvalidationinterfacequeue")
    gui.wait_for_property("paymentRequestModal", "openedEditor.receipts.receivedAmountSatoshi", 0, timeout_ms=30000)
    gui.wait_for_property("paymentRequestModal", "openedEditor.receipts.fresh", True)
    gui.wait_for_property("savedRequestReceiptStatus", "text", "Awaiting payment")
    gui.wait_for_property("savedRequestHistoricalUseNotice", "visible", True)
    gui.wait_for_property("savedRequestCopyUriButton", "enabled", False)
    checkpoint("reorg removes current credit without rearming the used address")

    # Stop with the original view still open; no dialog dismissal or GUI
    # destruction is required for native shutdown and persisted receipt facts.
    checked_stop("before-restart")
    harness.start_gui()
    gui = harness.driver
    select_wallet(gui, "receipt-restart")
    gui.click("activityTabButton")
    gui.wait_for_property(f"activityRequest_{request_id}", "visible", True, timeout_ms=30000)
    gui.wait_for_property("activityFilterProxyModel", "busy", False)
    gui.wait_for_property("activityStack", "walletViewActive", True)
    inventory = gui.get_model_row("activityListView", "model")
    original_row = gui.get_model_row("activityListView", "model", 0, "requestId",
                                     model_id=inventory["modelId"], expected_rows=inventory["rowCount"])
    assert original_row["value"] == request_id, "The original pending request must own the first Activity row"
    assert gui.get_property(f"activityRequest_{request_id}", "requestId") == request_id
    if os.getenv("QML_TEST_ARTIFACTS"):
        output = Path(os.environ["QML_TEST_ARTIFACTS"]) / "typed-current-receipt-reorg-restart"
        output.mkdir(parents=True, exist_ok=True)
        (output / "restart-row-intent.json").write_text(json.dumps({
            "inventory": inventory, "original_row": original_row, "request_id": request_id,
            "address": address, "target": "activityListView[0].activityRowOpenButton",
            "row_geometry": {key: gui.get_property(f"activityRequest_{request_id}", key)
                             for key in ("x", "y", "width", "height", "visible")},
            "content_y": gui.get_property("activityListView", "contentY"),
        }, indent=2) + "\n", encoding="utf-8")
    # Target the actual row's AbstractButton, as the existing Activity restart
    # journey does. Clicking the passive Item can report a sent mouse event
    # without an activated signal when its center is clipped or intercepted.
    gui.click_list_item("activityListView", 0, "activityRowOpenButton")
    gui.wait_for_property("paymentRequestModal", "opened", True, timeout_ms=30000)
    gui.wait_for_property("paymentRequestModal", "openedEditor.receipts.fresh", True)
    assert gui.get_property("paymentRequestModal", "openedEditor.requestId") == request_id
    assert gui.get_property("paymentRequestModal", "openedEditor.address") == address
    assert gui.get_property("paymentRequestModal", "openedEditor.receipts.receivedAmountSatoshi") == 0
    assert gui.get_property("paymentRequestModal", "openedEditor.historicallyUsed") is True
    assert gui.get_property("paymentRequestModal", "openedEditor.privateNote.text") == "Original private receipt note"
    gui.wait_for_property("savedRequestReceiptStatus", "text", "Awaiting payment")
    gui.wait_for_property("savedRequestCopyUriButton", "enabled", False)
    checkpoint("exact original request and historical use survived process restart")

    rpc_call(harness.gui_rpc_port, "reconsiderblock", [block])
    rpc_call(harness.gui_rpc_port, "syncwithvalidationinterfacequeue")
    gui.wait_for_property("paymentRequestModal", "openedEditor.receipts.receivedAmountSatoshi", 5_000_000_000, timeout_ms=30000)
    gui.wait_for_property("paymentRequestModal", "openedEditor.receipts.fresh", True)
    gui.wait_for_property("savedRequestReceiptStatus", "text", "Payment received")
    assert gui.get_property("paymentRequestModal", "openedEditor.requestId") == request_id
    gui.wait_for_property("savedRequestCopyUriButton", "enabled", False)
    checkpoint("reconsidered receipt updates the retained original view")
    checked_stop("after-restart")


if __name__ == "__main__":
    raise SystemExit(run_scenarios('Wallet Activity and native export process journey', [
        wallet_scenario("request-receipt-restart-and-dialog", 5, receive_smoke),
        wallet_scenario("typed-current-receipt-reorg-restart", 5, typed_receipt_restart),
    ]))
