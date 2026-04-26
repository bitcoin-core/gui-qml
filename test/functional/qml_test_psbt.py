#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""End-to-end GUI tests for PSBT import routing into send review pages."""

import argparse
import base64
import os
import re
import sys
from datetime import datetime

from qml_test_harness import dump_qml_tree
from qml_wallet_test_lib import WalletFlowHarness, rpc_call, wait_for_rpc


def parse_args():
    parser = argparse.ArgumentParser(
        description="PSBT GUI functional test",
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
    screenshot_root = os.path.join(artifacts_root, f"qml_test_psbt-{timestamp}")
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

        case_dir = os.path.join(self.screenshot_root, self.case_name)
        filename = f"{self.index:02d}-{self._sanitize_label(label)}.png"
        screenshot_path = os.path.join(case_dir, filename)
        screenshot = gui.save_screenshot(screenshot_path)
        print(
            f"{prefix}: screenshot saved to {screenshot['path']} "
            f"({screenshot['width']}x{screenshot['height']})"
        )


def wait_for_wallet(gui, wallet_name, timeout_ms=20000):
    gui.wait_for_property("walletBadge", "loading", False, timeout_ms=timeout_ms)
    gui.wait_for_property("walletBadge", "text", wallet_name, timeout_ms=timeout_ms)
    gui.wait_for_property("walletBadge", "noWalletLoaded", False, timeout_ms=timeout_ms)


def create_wallet(rpc_port, wallet_name):
    rpc_call(rpc_port, "createwallet", {"wallet_name": wallet_name})


def fund_wallet(rpc_port, wallet_name):
    mining_address = rpc_call(rpc_port, "getnewaddress", ["psbt-mining"], wallet=wallet_name)
    rpc_call(rpc_port, "generatetoaddress", [101, mining_address])


def write_psbt_fixture(rpc_port, wallet_name, outputs, filename):
    response = rpc_call(
        rpc_port,
        "walletcreatefundedpsbt",
        [[], outputs, 0, {"fee_rate": 2}, True],
        wallet=wallet_name,
    )
    fixture_path = filename
    with open(fixture_path, "wb") as fixture:
        fixture.write(base64.b64decode(response["psbt"]))
    return fixture_path


def navigate_to_send(gui):
    gui.click("sendTabButton")
    gui.wait_for_property("sendOptionsButton", "visible", True, timeout_ms=10000)


def import_psbt(gui, psbt_path):
    gui.click("sendOptionsButton")
    gui.wait_for_property("sendImportPsbtFromFileButton", "visible", True, timeout_ms=5000)
    gui.set_text("psbtImportPathField", psbt_path)
    gui.click("sendImportPsbtFromFileButton")


def build_fixtures(harness):
    recipient_wallet = "psbt-recipient"
    unsupported_wallet = "psbt-unsupported"
    source_wallet = "psbt-source"

    create_wallet(harness.gui_rpc_port, recipient_wallet)
    create_wallet(harness.gui_rpc_port, unsupported_wallet)
    create_wallet(harness.gui_rpc_port, source_wallet)

    recipient_address_1 = rpc_call(
        harness.gui_rpc_port, "getnewaddress", ["psbt-destination-1"], wallet=recipient_wallet
    )
    recipient_address_2 = rpc_call(
        harness.gui_rpc_port, "getnewaddress", ["psbt-destination-2"], wallet=recipient_wallet
    )

    fund_wallet(harness.gui_rpc_port, unsupported_wallet)
    fund_wallet(harness.gui_rpc_port, source_wallet)

    single_review_path = write_psbt_fixture(
        harness.gui_rpc_port,
        source_wallet,
        [{recipient_address_1: 1.0}],
        os.path.join(harness.tmpdir, "single-review.psbt"),
    )
    multiple_review_path = write_psbt_fixture(
        harness.gui_rpc_port,
        source_wallet,
        [{recipient_address_1: 1.0}, {recipient_address_2: 0.5}],
        os.path.join(harness.tmpdir, "multiple-review.psbt"),
    )
    unsupported_path = write_psbt_fixture(
        harness.gui_rpc_port,
        unsupported_wallet,
        [{recipient_address_1: 0.75}],
        os.path.join(harness.tmpdir, "unsupported.psbt"),
    )
    return {
        "source_wallet": source_wallet,
        "single_review_path": single_review_path,
        "multiple_review_path": multiple_review_path,
        "unsupported_path": unsupported_path,
    }


def run_test():
    args = parse_args()
    screenshot_root = None
    if args.save_screenshots:
        screenshot_root = make_screenshot_root()
        print(f"Checkpoint screenshots will be saved under: {screenshot_root}")

    harness = WalletFlowHarness("qml_test_psbt", port_offset=40)
    checkpoints = CheckpointRecorder("psbt-import-routing", args.save_screenshots, screenshot_root)
    gui = None
    try:
        checkpoints.checkpoint("starting harness")
        harness.start_gui()
        gui = harness.driver
        checkpoints.checkpoint("GUI launched", gui)
        wait_for_rpc(harness.gui_rpc_port)
        checkpoints.checkpoint("GUI RPC ready", gui)

        fixtures = build_fixtures(harness)
        checkpoints.checkpoint("PSBT fixtures created")
        wait_for_wallet(gui, fixtures["source_wallet"])
        checkpoints.checkpoint(f"wallet selected: {fixtures['source_wallet']}", gui)

        navigate_to_send(gui)
        checkpoints.checkpoint("send page opened", gui)

        import_psbt(gui, fixtures["single_review_path"])
        checkpoints.checkpoint("single-recipient PSBT submitted", gui)
        gui.wait_for_page("sendReviewPage", timeout_ms=20000)
        assert gui.get_current_page() == "sendReviewPage", "Expected single-recipient PSBT to open SendReview"
        checkpoints.checkpoint("single-recipient review displayed", gui)
        gui.click("sendReviewBackButton")
        gui.wait_for_property("sendOptionsButton", "visible", True, timeout_ms=10000)
        checkpoints.checkpoint("returned from single review to send page", gui)

        import_psbt(gui, fixtures["multiple_review_path"])
        checkpoints.checkpoint("multi-recipient PSBT submitted", gui)
        gui.wait_for_page("multipleSendReviewPage", timeout_ms=20000)
        assert gui.get_current_page() == "multipleSendReviewPage", (
            "Expected multi-recipient PSBT to open MultipleSendReview"
        )
        checkpoints.checkpoint("multiple-recipient review displayed", gui)
        gui.click("multipleSendReviewBackButton")
        gui.wait_for_property("sendOptionsButton", "visible", True, timeout_ms=10000)
        checkpoints.checkpoint("returned from multiple review to send page", gui)

        import_psbt(gui, fixtures["unsupported_path"])
        checkpoints.checkpoint("unsupported PSBT submitted", gui)
        gui.wait_for_property("unsupportedPsbtPopup", "visible", True, timeout_ms=20000)
        checkpoints.checkpoint("unsupported PSBT modal displayed", gui)
        assert gui.get_text("unsupportedPsbtPopupTitle") == "Not supported"
        assert gui.get_text("unsupportedPsbtPopupMessage") == (
            "Only PSBTs that spend this wallet's own inputs are supported right now."
        )
        gui.click("unsupportedPsbtPopupOkButton")
        gui.wait_for_property("unsupportedPsbtPopup", "visible", False, timeout_ms=10000)
        gui.wait_for_property("sendOptionsButton", "visible", True, timeout_ms=10000)
        checkpoints.checkpoint("unsupported PSBT modal dismissed", gui)

        print("\n" + "=" * 50)
        print("All tests PASSED")
        print("=" * 50)
        return 0
    except Exception as err:  # noqa: BLE001 - functional test should preserve context
        print(f"\nFAILED: {err}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        if gui is not None:
            try:
                checkpoints.checkpoint("failure state", gui)
            except Exception as screenshot_err:  # noqa: BLE001 - preserve original failure context
                print(f"failed to save failure screenshot: {screenshot_err}", file=sys.stderr)
            dump_qml_tree(gui)
        gui_output = harness.process_output(harness.gui_process)
        if gui_output:
            print("\n--- GUI process output ---", file=sys.stderr)
            print(gui_output, file=sys.stderr)
        return 1
    finally:
        harness.stop()


if __name__ == "__main__":
    sys.exit(run_test())
