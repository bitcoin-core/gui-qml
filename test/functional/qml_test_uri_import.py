#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""End-to-end QML URI import test.

Exercises clipboard paste, manual popup entry, malformed URI error feedback,
file import, and drag-drop simulation via the test automation hooks.

Requires:
  - bitcoin-core-app built with -DENABLE_TEST_AUTOMATION=ON
  - bitcoind built with -DBUILD_DAEMON=ON
"""

import sys
from pathlib import Path
from tempfile import NamedTemporaryFile

from qml_test_harness import dump_qml_tree
from qml_wallet_test_lib import WalletFlowHarness, rpc_call

WALLET_NAME = "testwallet"


def navigate_to_send(gui):
    """Click the Send tab and wait for the Send page to appear."""
    gui.click("sendTabButton")
    gui.wait_for_page("sendPage", timeout_ms=15000)
    gui.wait_for_property("walletSendTitle", "visible", True, timeout_ms=5000)


def open_send_options(gui):
    """Open the Send options (ellipsis) popup."""
    gui.click("sendOptionsButton")
    gui.wait_for_property("sendOptionsPopup", "opened", True, timeout_ms=5000)


def run_tests():
    harness = WalletFlowHarness("qml_test_uri_import", port_offset=500)
    gui = None
    try:
        harness.start_gui()
        gui = harness.driver

        gui.wait_for_property("walletBadge", "loading", False, timeout_ms=20000)
        gui.wait_for_property("walletBadge", "visible", True, timeout_ms=10000)
        rpc_call(harness.gui_rpc_port, "createwallet", {"wallet_name": WALLET_NAME})
        gui.wait_for_property("walletBadge", "text", WALLET_NAME, timeout_ms=20000)
        gui.wait_for_property("walletBadge", "noWalletLoaded", False, timeout_ms=10000)

        target_address = rpc_call(
            harness.gui_rpc_port,
            "getnewaddress",
            ["uri-test", "bech32"],
            wallet=WALLET_NAME,
        )
        print(f"Test address: {target_address}")

        navigate_to_send(gui)
        print("Send page open.")

        # ----------------------------------------------------------------
        # Test 1: Clipboard banner — "Fill" button
        # The banner appears automatically when a valid bitcoin: URI is
        # detected in the clipboard. Clicking "Fill" applies the URI fields.
        # ----------------------------------------------------------------
        uri_clip = (
            f"bitcoin:{target_address}"
            f"?amount=0.01234567&label=clip-label&message=clipboard-note"
        )
        gui.set_clipboard_text(uri_clip)
        gui.wait_for_property("clipboardUriPasteButton", "visible", True, timeout_ms=5000)
        gui.click("clipboardUriPasteButton")
        gui.wait_for_property(
            "sendPaymentRequestStatusText", "text",
            lambda v: "clipboard" in str(v), timeout_ms=10000,
        )
        gui.wait_for_property(
            "sendPaymentRequestPayTo", "value",
            lambda v: "clip-label" in str(v), timeout_ms=5000,
        )
        gui.wait_for_property(
            "sendPaymentRequestMessageText", "value",
            lambda v: "clipboard-note" in str(v), timeout_ms=5000,
        )
        print("Test 1 PASSED: clipboard banner Fill button.")

        # ----------------------------------------------------------------
        # Test 2: Manual URI popup
        # ----------------------------------------------------------------
        uri_manual = (
            f"bitcoin:{target_address}"
            f"?amount=0.02000000&label=manual-label&message=manual-note"
        )
        open_send_options(gui)
        gui.click("sendOptionsOpenPaymentRequestButton")
        gui.wait_for_property("sendUriImportPopup", "opened", True, timeout_ms=5000)
        gui.set_text("sendUriImportInput", uri_manual)
        gui.click("sendUriImportApplyButton")
        gui.wait_for_property("sendUriImportPopup", "opened", False, timeout_ms=5000)
        gui.wait_for_property(
            "sendPaymentRequestStatusText", "text",
            lambda v: "manual entry" in str(v), timeout_ms=10000,
        )
        gui.wait_for_property(
            "sendPaymentRequestPayTo", "value",
            lambda v: "manual-label" in str(v), timeout_ms=5000,
        )
        print("Test 2 PASSED: manual URI popup.")

        # ----------------------------------------------------------------
        # Test 3: Malformed URI shows error (via manual entry popup)
        # A bitcoin:// URI is invalid; the banner never appears for it
        # (parser returns success=False), so the error path is exercised
        # through the "Open payment request" popup instead.
        # ----------------------------------------------------------------
        malformed = f"bitcoin://{target_address}?amount=0.1"
        open_send_options(gui)
        gui.click("sendOptionsOpenPaymentRequestButton")
        gui.wait_for_property("sendUriImportPopup", "opened", True, timeout_ms=5000)
        gui.set_text("sendUriImportInput", malformed)
        gui.click("sendUriImportApplyButton")
        gui.wait_for_property("sendUriImportPopup", "opened", False, timeout_ms=5000)
        gui.wait_for_property(
            "sendPaymentRequestStatusText", "text",
            lambda v: "bitcoin://" in str(v), timeout_ms=10000,
        )
        print("Test 3 PASSED: malformed URI shows error.")

        # ----------------------------------------------------------------
        # Test 4: File import via automation hook
        # ----------------------------------------------------------------
        uri_file = (
            f"bitcoin:{target_address}"
            f"?amount=0.03000000&label=file-label&message=file-note"
        )
        with NamedTemporaryFile("w", suffix=".txt", delete=False, encoding="utf8") as tmp:
            tmp.write(uri_file)
            tmp_path = Path(tmp.name)
        try:
            gui.set_text("sendImportPaymentRequestFilePathInput", str(tmp_path))
            gui.click("sendApplyPaymentRequestFilePathButton")
            gui.wait_for_property(
                "sendPaymentRequestStatusText", "text",
                lambda v: "file" in str(v), timeout_ms=10000,
            )
            gui.wait_for_property(
                "sendPaymentRequestPayTo", "value",
                lambda v: "file-label" in str(v), timeout_ms=5000,
            )
            print("Test 4 PASSED: file import.")
        finally:
            tmp_path.unlink(missing_ok=True)

        # ----------------------------------------------------------------
        # Test 5: Drag-drop simulation via automation hook
        # ----------------------------------------------------------------
        uri_drop = (
            f"bitcoin:{target_address}"
            f"?amount=0.04000000&label=drop-label"
        )
        gui.set_text("sendDropUriInput", uri_drop)
        gui.click("sendApplyDropUriButton")
        gui.wait_for_property(
            "sendPaymentRequestStatusText", "text",
            lambda v: "drag and drop" in str(v), timeout_ms=10000,
        )
        gui.wait_for_property(
            "sendPaymentRequestPayTo", "value",
            lambda v: "drop-label" in str(v), timeout_ms=5000,
        )
        print("Test 5 PASSED: drag-drop simulation.")

        # ----------------------------------------------------------------
        # Test 6: DropArea hasUrls + file:// branch
        # Exercises: strip "file://" prefix → applyPaymentRequestFromFile
        # ----------------------------------------------------------------
        uri_drop_file = (
            f"bitcoin:{target_address}"
            f"?amount=0.05000000&label=drop-file"
        )
        with NamedTemporaryFile("w", suffix=".txt", delete=False, encoding="utf8") as tmp:
            tmp.write(uri_drop_file)
            tmp_path = Path(tmp.name)
        try:
            gui.set_text("sendDropFileUrlInput", tmp_path.as_uri())
            gui.click("sendApplyDropFileUrlButton")
            gui.wait_for_property(
                "sendPaymentRequestStatusText", "text",
                lambda v: "file" in str(v), timeout_ms=10000,
            )
            gui.wait_for_property(
                "sendPaymentRequestPayTo", "value",
                lambda v: "drop-file" in str(v), timeout_ms=5000,
            )
            print("Test 6 PASSED: DropArea hasUrls + file:// branch.")
        finally:
            tmp_path.unlink(missing_ok=True)

        # ----------------------------------------------------------------
        # Test 7: DropArea hasUrls + non-file URL branch
        # Exercises: non-file URL → applyPaymentRequestFromText("drag and drop")
        # ----------------------------------------------------------------
        uri_drop_url = (
            f"bitcoin:{target_address}"
            f"?amount=0.06000000&label=drop-url"
        )
        gui.set_text("sendDropFileUrlInput", uri_drop_url)
        gui.click("sendApplyDropFileUrlButton")
        gui.wait_for_property(
            "sendPaymentRequestStatusText", "text",
            lambda v: "drag and drop" in str(v), timeout_ms=10000,
        )
        gui.wait_for_property(
            "sendPaymentRequestPayTo", "value",
            lambda v: "drop-url" in str(v), timeout_ms=5000,
        )
        print("Test 7 PASSED: DropArea hasUrls + non-file URL branch.")

        # ----------------------------------------------------------------
        # Test 8: Native text-control paste from every recipient field
        # Calling paste() exercises the same control path as a context-menu
        # Paste action without assuming a platform-specific keyboard shortcut.
        # ----------------------------------------------------------------
        amount_field_uri = (
            f"bitcoin:{target_address}"
            f"?amount=0.07000000&label=drop-url"
        )
        gui.set_clipboard_text(amount_field_uri)
        gui.click("sendAmountInput")
        gui.invoke("sendAmountInput", "paste")
        gui.wait_for_property(
            "sendAmountInput", "text",
            lambda v: "0.07000000" in str(v), timeout_ms=5000,
        )

        label_field_uri = (
            f"bitcoin:{target_address}"
            f"?amount=0.07000000&label=field-paste"
        )
        gui.set_clipboard_text(label_field_uri)
        gui.click("sendNoteInput")
        gui.invoke("sendNoteInput", "paste")
        gui.wait_for_property(
            "sendPaymentRequestPayTo", "value", "field-paste", timeout_ms=5000,
        )

        address_field_uri = (
            f"bitcoin:{target_address}"
            f"?amount=0.07000000&label=field-paste&message=address-field-paste"
        )
        gui.set_clipboard_text(address_field_uri)
        gui.click("sendAddressInput")
        gui.invoke("sendAddressInput", "paste")
        gui.wait_for_property(
            "sendPaymentRequestMessageText", "value",
            "address-field-paste", timeout_ms=5000,
        )
        print("Test 8 PASSED: native paste from recipient fields.")

        # ----------------------------------------------------------------
        # Test 9: Non-URI context-menu paste keeps normal text behavior
        # ----------------------------------------------------------------
        gui.set_clipboard_text("-ordinary-paste")
        gui.click("sendNoteInput")
        gui.invoke("sendNoteInput", "paste")
        gui.wait_for_property(
            "sendNoteInput", "text",
            lambda v: "-ordinary-paste" in str(v), timeout_ms=5000,
        )
        print("Test 9 PASSED: non-URI native paste remains unchanged.")

        print("\n" + "=" * 50)
        print("All URI import tests PASSED (9/9)")
        print("=" * 50)

    except Exception as exc:
        print(f"\nFAILED: {exc}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        if gui is not None:
            dump_qml_tree(gui)
        output = harness.process_output(harness.gui_process)
        if output:
            print("\n--- GUI process output ---", file=sys.stderr)
            print(output, file=sys.stderr)
        sys.exit(1)
    finally:
        harness.stop()


if __name__ == "__main__":
    run_tests()
