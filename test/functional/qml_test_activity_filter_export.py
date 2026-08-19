#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""End-to-end GUI test for Activity search, filters, and CSV export."""

import argparse
import os
import re
import sys
import time
from datetime import datetime
from urllib.parse import urlparse

from qml_test_harness import dump_qml_tree
from qml_test_receive import WALLET_NAME, _create_request, _import_wallet, _open_receive, _request_qr_payload
from qml_wallet_test_lib import WalletFlowHarness, rpc_call, wait_for_rpc


def parse_args():
    parser = argparse.ArgumentParser(
        description="Activity search/filter/export GUI functional test",
        add_help=True,
    )
    parser.add_argument(
        "--save-screenshots",
        action="store_true",
        help="Save a PNG at each GUI checkpoint under test/artifacts/",
    )
    return parser.parse_args()


def make_screenshot_root():
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
    artifacts_root = os.path.join(repo_root, "test", "artifacts")
    timestamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    screenshot_root = os.path.join(artifacts_root, f"qml_test_activity_filter_export-{timestamp}")
    os.makedirs(screenshot_root, exist_ok=True)
    return screenshot_root


class CheckpointRecorder:
    def __init__(self, case_name, save_screenshots, screenshot_root):
        self.case_name = case_name
        self.save_screenshots = save_screenshots
        self.screenshot_root = screenshot_root
        self.index = 0

    def _sanitize_label(self, label):
        return re.sub(r"[^a-z0-9]+", "-", label.lower()).strip("-") or "checkpoint"

    def checkpoint(self, label, gui=None):
        self.index += 1
        prefix = f"[{self.case_name}] checkpoint {self.index:02d}"
        print(f"{prefix}: {label}")
        if gui is None:
            return

        gui.settle()

        if not self.save_screenshots:
            return

        filename = f"{self.index:02d}-{self._sanitize_label(label)}.png"
        screenshot_path = os.path.join(self.screenshot_root, filename)
        screenshot = gui.save_screenshot(screenshot_path)
        print(
            f"{prefix}: screenshot saved to {screenshot['path']} "
            f"({screenshot['width']}x{screenshot['height']})"
        )


def _wait_for_file(path, timeout=5):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if os.path.isfile(path):
            return
        time.sleep(0.1)
    raise AssertionError(f"Timed out waiting for exported CSV: {path}")


def _export_activity_csv(gui, path):
    try:
        os.unlink(path)
    except FileNotFoundError:
        pass
    gui.set_text("activityExportPathField", path)
    gui.click("activityExportButton")
    _wait_for_file(path)
    gui.wait_for_property("activityExportResultTitle", "text", "Export complete", timeout_ms=10000)
    gui.wait_for_property(
        "activityExportResultDescription",
        "text",
        "Your Activity CSV has been saved.",
        timeout_ms=10000,
    )
    with open(path, "r", encoding="utf8") as csv_file:
        return csv_file.read()


def _address_from_bip21(uri):
    parsed = urlparse(uri)
    assert parsed.scheme == "bitcoin", f"Unexpected BIP21 scheme: {uri!r}"
    assert parsed.path, f"BIP21 URI missing address: {uri!r}"
    return parsed.path


def _mine_to_gui_wallet(harness):
    wait_for_rpc(harness.gui_rpc_port)
    address = rpc_call(
        harness.gui_rpc_port,
        "getnewaddress",
        {"label": "Mining reward"},
        wallet=WALLET_NAME,
    )
    blocks = rpc_call(harness.gui_rpc_port, "generatetoaddress", [1, address])
    assert len(blocks) == 1, f"Expected one generated block, got {blocks!r}"
    return address


def _set_activity_search_visible(gui, visible):
    if gui.get_property("activitySearchToggle", "checked") != visible:
        gui.click("activitySearchToggle")
    gui.wait_for_property("activitySearchToggle", "checked", visible, timeout_ms=5000)


def _activity_transaction_row(gui):
    row_count = int(gui.get_property("activityListView", "count"))
    for row in range(row_count):
        gui.set_property("activityListView", "currentIndex", row)
        gui.invoke("activityListView", "forceLayout")
        gui.settle()
        txid = gui.get_list_item_property("activityListView", row, "txid")
        if txid:
            return txid, gui.get_list_item_property("activityListView", row, "amount")
    raise AssertionError("Expected Activity list to contain a transaction row")


def run_test(save_screenshots=False, screenshot_root=None):
    case_name = "qml_activity_filter_export"
    harness = WalletFlowHarness(case_name, port_offset=90)
    checkpoints = CheckpointRecorder(case_name, save_screenshots, screenshot_root)
    try:
        print(f"[{case_name}] starting")
        if save_screenshots:
            print(f"[{case_name}] checkpoint screenshots will be saved under: {screenshot_root}")
        gui = _import_wallet(harness)
        checkpoints.checkpoint("wallet imported", gui)

        gui.click("activityTabButton")
        gui.wait_for_property("activityFilterProxyModel", "count", 0, timeout_ms=20000)
        gui.wait_for_property("activityEmptyState", "visible", True, timeout_ms=10000)
        source_empty_title = gui.get_property("activityEmptyStateTitle", "text")
        source_empty_description = gui.get_property("activityEmptyStateDescription", "text")
        assert source_empty_title in (
            "No activity yet",
            "Syncing wallet activity...",
        ), f"Unexpected source-empty Activity title: {source_empty_title!r}"
        assert source_empty_description in (
            "Once you send or receive bitcoin, your transactions will appear here.",
            "Transactions may appear as your wallet catches up.",
        ), f"Unexpected source-empty Activity description: {source_empty_description!r}"
        checkpoints.checkpoint(f"source-empty Activity state shown: {source_empty_title}", gui)

        _open_receive(gui)
        checkpoints.checkpoint("receive page opened", gui)

        _create_request(gui, "0.0001", "Alice", "pizza")
        payment_request_uri = _request_qr_payload(gui)
        payment_request_address = _address_from_bip21(payment_request_uri)
        checkpoints.checkpoint("payment request created", gui)

        mined_address = _mine_to_gui_wallet(harness)
        assert mined_address != payment_request_address, "Mined row should not consume the pending request row"
        checkpoints.checkpoint("block mined to wallet address", gui)

        gui.click("activityTabButton")
        gui.wait_for_property("activityFilterProxyModel", "count", 2, timeout_ms=20000)
        checkpoints.checkpoint("Activity shows payment request and mined rows", gui)

        _set_activity_search_visible(gui, True)
        checkpoints.checkpoint("Activity search controls opened", gui)

        gui.set_text("activitySearchField", payment_request_address)
        gui.wait_for_property("activityFilterProxyModel", "count", 1, timeout_ms=10000)
        checkpoints.checkpoint("search by request address applied", gui)

        gui.set_text("activitySearchField", "")
        gui.wait_for_property("activityFilterProxyModel", "count", 2, timeout_ms=10000)

        gui.click("activityDateFilterButton")
        checkpoints.checkpoint("date filter menu opened", gui)
        gui.click("activityDateToday")

        gui.click("activityTypeFilterButton")
        checkpoints.checkpoint("type filter menu opened", gui)
        gui.click("activityTypeMined")
        gui.wait_for_property("activityFilterProxyModel", "count", 1, timeout_ms=10000)
        checkpoints.checkpoint("Today and Mined filters applied", gui)

        mined_export_path = os.path.join(harness.tmpdir, "activity-mined.csv")
        mined_csv = _export_activity_csv(gui, mined_export_path)
        checkpoints.checkpoint("mined Activity row exported to CSV", gui)

        expected_btc_header = '"Confirmed","Date","Type","Label","Address","Amount (BTC)","ID"\n'
        expected_sat_header = '"Confirmed","Date","Type","Label","Address","Amount (sat)","ID"\n'
        assert mined_csv.startswith(expected_btc_header), f"Unexpected mined CSV header: {mined_csv!r}"
        assert '"Mined"' in mined_csv, f"Missing mined type: {mined_csv!r}"
        assert '"Mining reward"' in mined_csv, f"Missing mined label: {mined_csv!r}"
        assert '"50.00000000"' in mined_csv, f"Missing mined amount: {mined_csv!r}"
        assert '"Payment request"' not in mined_csv, f"Mined export included request row: {mined_csv!r}"
        gui.click("activityExportResultCloseButton")

        gui.click("activityTypeFilterButton")
        gui.click("activityTypePaymentRequest")
        gui.wait_for_property("activityFilterProxyModel", "count", 1, timeout_ms=10000)
        checkpoints.checkpoint("Today and Payment request filters applied", gui)

        gui.set_text("activitySearchField", "Alice")
        gui.wait_for_property("activityFilterProxyModel", "count", 1, timeout_ms=10000)
        checkpoints.checkpoint("search by request label applied", gui)

        gui.click("desktopWalletSettingsTabButton")
        gui.wait_for_property("settingsSidebar_display", "visible", True, timeout_ms=5000)
        gui.click("settingsSidebar_display")
        gui.wait_for_page("settingsv2DisplayUnitPicker", timeout_ms=5000)
        gui.click("settingsv2DisplayUnitPickerButton")
        gui.wait_for_page("settingsv2DisplayUnitSAT", timeout_ms=5000)
        gui.click("settingsv2DisplayUnitSAT")
        checkpoints.checkpoint("display unit switched to sats", gui)

        gui.click("activityTabButton")
        gui.wait_for_property("activityFilterProxyModel", "displayUnit", 3, timeout_ms=5000)
        gui.wait_for_property("activityFilterProxyModel", "count", 1, timeout_ms=10000)
        checkpoints.checkpoint("returned to filtered Activity in sats", gui)

        request_export_path = os.path.join(harness.tmpdir, "activity-request.csv")
        csv = _export_activity_csv(gui, request_export_path)
        checkpoints.checkpoint("filtered Activity exported to CSV", gui)

        assert csv.startswith(expected_sat_header), f"Unexpected CSV header: {csv!r}"
        assert '"Payment request"' in csv, f"Missing payment request type: {csv!r}"
        assert '"Alice"' in csv, f"Missing request label: {csv!r}"
        assert f'"{payment_request_address}"' in csv, f"Missing request address: {csv!r}"
        assert '"10000"' in csv, f"Missing amount: {csv!r}"
        assert '"Mined"' not in csv, f"Payment request export included mined row: {csv!r}"
        gui.click("activityExportResultCloseButton")

        gui.set_text("activitySearchField", "not-present")
        gui.wait_for_property("activityFilterProxyModel", "count", 0, timeout_ms=10000)
        gui.wait_for_property(
            "activityEmptyStateTitle",
            "text",
            "No activity matches your filters.",
            timeout_ms=10000,
        )
        gui.wait_for_property(
            "activityEmptyStateDescription",
            "text",
            "Try changing your search, date, or type filters.",
            timeout_ms=10000,
        )
        checkpoints.checkpoint("filtered-empty Activity state shown", gui)

        gui.click("activitySearchToggle")
        gui.wait_for_property("activitySearchToggle", "checked", False, timeout_ms=5000)
        gui.wait_for_property("activityFilterProxyModel", "searchText", "", timeout_ms=5000)
        date_filter = gui.get_property("activityFilterProxyModel", "dateFilter")
        type_filter = gui.get_property("activityFilterProxyModel", "typeFilter")
        assert date_filter in (0, "DateAll"), f"Date filter was not reset: {date_filter!r}"
        assert type_filter in (0, "TypeAll"), f"Type filter was not reset: {type_filter!r}"
        gui.wait_for_property("activityFilterProxyModel", "count", 2, timeout_ms=10000)
        checkpoints.checkpoint("search controls closed and filters reset", gui)

        txid, expected_amount = _activity_transaction_row(gui)
        gui.click(f"activityItem_{txid}")
        gui.wait_for_page("activityDetailsPage", timeout_ms=10000)
        actual_amount = gui.get_property("activityDetailsPage", "amount")
        assert actual_amount == expected_amount, (
            "Activity details did not use the selected display unit: "
            f"expected {expected_amount!r}, got {actual_amount!r}"
        )
        checkpoints.checkpoint("Activity details amount uses selected display unit", gui)

        print(f"[{case_name}] completed")
        return 0
    except Exception as err:  # noqa: BLE001 - preserve failure context
        print(f"\nFAILED [{case_name}]: {err}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        gui = harness.driver
        gui_process = harness.gui_process
        if gui is not None:
            try:
                checkpoints.checkpoint("failure state", gui)
            except Exception as screenshot_err:  # noqa: BLE001 - preserve original failure context
                print(f"[{case_name}] failed to save failure screenshot: {screenshot_err}", file=sys.stderr)
            dump_qml_tree(gui)
        harness.stop()
        gui_output = harness.process_output(gui_process)
        if gui_output:
            print("\n--- GUI process output ---", file=sys.stderr)
            print(gui_output, file=sys.stderr)
        return 1
    finally:
        harness.stop()


if __name__ == "__main__":
    args = parse_args()
    root = make_screenshot_root() if args.save_screenshots else None
    sys.exit(run_test(save_screenshots=args.save_screenshots, screenshot_root=root))
