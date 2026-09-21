#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Reverse label sync: editing an address label on the Addresses page updates the
matching payment request and Activity search while preserving private notes."""

import sys

from qml_test_harness import dump_qml_tree
from qml_test_receive import (
    _address_from_bip21,
    _create_request,
    _import_wallet,
    _open_activity,
    _open_receive,
    _request_qr_payload,
)
from qml_test_addresses import open_address_list_from_settings
from qml_wallet_test_lib import WalletFlowHarness

ORIGINAL_LABEL = "Alice"
NEW_LABEL = "Renamed on Addresses page"
PRIVATE_NOTE = "Private lunch note"


def _address_row_index(gui, address):
    count = gui.get_property("addressListView", "count")
    for row in range(count):
        if gui.get_list_item_property("addressListView", row, "address") == address:
            return row
    raise AssertionError(f"Address {address!r} not found in the address list")


def _activity_request_label(gui):
    # No funding in this flow, so the pending request is the only Activity row.
    gui.wait_for_property("activityListView", "count", lambda c: c >= 1, timeout_ms=10000)
    # Force the delegate to instantiate offscreen before reading its label.
    gui.set_property("activityListView", "currentIndex", 0)
    gui.invoke("activityListView", "forceLayout")
    gui.settle()
    return gui.get_property("activityRequest_1", "label")


def run_test():
    harness = WalletFlowHarness("qml_address_label_sync", port_offset=75)
    try:
        gui = _import_wallet(harness)

        # Save a payment request labelled ORIGINAL_LABEL; creating it also labels
        # its receive address in the address book.
        _open_receive(gui)
        _create_request(gui, "0.0001", ORIGINAL_LABEL, "pizza", note_self=PRIVATE_NOTE)
        request_address = _address_from_bip21(_request_qr_payload(gui))

        # Activity displays the private note, independently of the public name.
        _open_activity(gui)
        assert _activity_request_label(gui) == PRIVATE_NOTE, (
            f"Expected private Activity note {PRIVATE_NOTE!r}, "
            f"got {_activity_request_label(gui)!r}"
        )

        # Edit the label on the Addresses page.
        open_address_list_from_settings(gui)
        row = _address_row_index(gui, request_address)
        assert gui.get_list_item_property("addressListView", row, "label") == ORIGINAL_LABEL
        gui.click_list_item("addressListView", row, "addressRowNoteField")
        gui.set_text("addressRowNoteField", NEW_LABEL)
        gui.settle()

        # Surface 1: the Addresses page reflects the edit.
        row = _address_row_index(gui, request_address)
        assert gui.get_list_item_property("addressListView", row, "label") == NEW_LABEL, (
            "Addresses page did not show the edited label"
        )

        # Surface 2: Activity search follows the updated name without changing its private note.
        _open_activity(gui)
        gui.set_text("activitySearchField", NEW_LABEL)
        gui.wait_for_property("activityFilterProxyModel", "count", 1, timeout_ms=10000)
        actual = _activity_request_label(gui)
        assert actual == PRIVATE_NOTE, (
            "Address label edit changed the private Activity note: "
            f"expected {PRIVATE_NOTE!r}, got {actual!r}"
        )

        # Surface 3: reopening the saved request shows the synced label in
        # the shared modal while leaving Activity underneath.
        gui.click_list_item("activityListView", 0, "activityRowOpenButton")
        gui.wait_for_property("paymentRequestModal", "opened", True)
        assert gui.get_property("requestPaymentLabelRow", "value") == NEW_LABEL
        assert gui.get_property("requestPaymentNoteRow", "value") == PRIVATE_NOTE

        print("Address label reverse-sync flow passed.")
        return 0
    except Exception as err:  # noqa: BLE001 - preserve GUI context on failures
        print(f"\nFAILED [qml_address_label_sync]: {err}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        try:
            dump_qml_tree(harness.driver)
        except Exception:  # noqa: BLE001
            pass
        return 1
    finally:
        harness.stop()


if __name__ == "__main__":
    sys.exit(run_test())
