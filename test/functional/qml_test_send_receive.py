#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""End-to-end GUI test for send preview fee parity."""

import argparse
from decimal import Decimal, InvalidOperation
from datetime import datetime
import os
import re
import sys
import time

from qml_test_harness import dump_qml_tree
from qml_wallet_test_lib import WalletFlowHarness, rpc_call


SEND_AMOUNT = "1.00000000"
SEND_AMOUNT_SATS = 100000000
GUI_WALLET_NAME = "send_flow_wallet"
RECEIVER_WALLET_NAME = "send_flow_receiver"
DEFAULT_FEE_LABEL = "Default"
DEFAULT_FEE_DURATION = "(~20 mins)"
LOW_FEE_LABEL = "Low"
LOW_FEE_DURATION = "(~60 mins)"
LOW_FEE_OPTION_INDEX = 2
LOW_FEE_TARGET_BLOCKS = 6


def parse_args():
    parser = argparse.ArgumentParser(
        description="Send/receive GUI functional test",
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
    screenshot_root = os.path.join(artifacts_root, f"qml_test_send_receive-{timestamp}")
    os.makedirs(screenshot_root, exist_ok=True)
    return screenshot_root


class CheckpointRecorder:
    def __init__(self, save_screenshots, screenshot_root):
        self.save_screenshots = save_screenshots
        self.screenshot_root = screenshot_root
        self.index = 0

    def _sanitize_label(self, label):
        return re.sub(r"[^a-z0-9]+", "-", label.lower()).strip("-") or "checkpoint"

    def checkpoint(self, label, gui=None):
        self.index += 1
        prefix = f"[qml_test_send_receive] checkpoint {self.index:02d}"
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


def assert_review_values(gui, *, expected_fee_sats, expected_amount_sats, expected_total_sats):
    review_amount_text = gui.get_text("sendReviewAmountField")
    review_fee_text = gui.get_text("sendReviewFeeField")
    review_total_text = gui.get_text("sendReviewTotalField")

    review_amount_sats = amount_text_to_sats(review_amount_text)
    review_fee_sats = amount_text_to_sats(review_fee_text)
    review_total_sats = amount_text_to_sats(review_total_text)

    assert review_fee_sats == expected_fee_sats, (
        f"Preview fee {expected_fee_sats} sats did not match review fee "
        f"{review_fee_text!r} ({review_fee_sats} sats)"
    )
    assert review_amount_sats == expected_amount_sats, (
        f"Expected review amount {expected_amount_sats} sats, got "
        f"{review_amount_text!r} ({review_amount_sats} sats)"
    )
    assert review_total_sats == expected_total_sats, (
        f"Expected review total {expected_total_sats} sats, got "
        f"{review_total_text!r} ({review_total_sats} sats)"
    )


def wait_until(predicate, *, timeout=20, interval=0.1, description="condition"):
    deadline = time.time() + timeout
    last_error = None
    while time.time() < deadline:
        try:
            if predicate():
                return
        except Exception as err:  # noqa: BLE001 - retry helper preserves latest error context
            last_error = err
        time.sleep(interval)
    if last_error is not None:
        raise AssertionError(f"Timed out waiting for {description}: {last_error}") from last_error
    raise AssertionError(f"Timed out waiting for {description}")


def wait_for_non_empty_text(gui, object_name, *, timeout=20):
    value = ""

    def has_text():
        nonlocal value
        value = gui.get_text(object_name).strip()
        return bool(value)

    wait_until(has_text, timeout=timeout, description=f"{object_name} text")
    return value


def btc_text_to_sats(text):
    match = re.search(r"([0-9]+(?:\.[0-9]+)?)", text.replace(",", ""))
    if match is None:
        raise AssertionError(f"Could not parse BTC amount from {text!r}")
    try:
        return int((Decimal(match.group(1)) * Decimal("100000000")).to_integral_value())
    except InvalidOperation as err:
        raise AssertionError(f"Invalid BTC amount {text!r}") from err


def sat_text_to_sats(text):
    match = re.search(r"(-?[0-9]+)", text.replace(",", ""))
    if match is None:
        raise AssertionError(f"Could not parse satoshi amount from {text!r}")
    return int(match.group(1))


def amount_text_to_sats(text):
    return sat_text_to_sats(text) if "sat" in text else btc_text_to_sats(text)


def wait_for_wallet_balance(port, wallet_name, *, minimum_balance):
    balance = Decimal("0")

    def has_balance():
        nonlocal balance
        balance = Decimal(str(rpc_call(port, "getbalance", wallet=wallet_name)))
        return balance >= minimum_balance

    wait_until(
        has_balance,
        timeout=30,
        interval=0.25,
        description=f"{wallet_name} balance >= {minimum_balance}",
    )
    return balance


def wait_for_single_mempool_tx(port):
    txids = []

    def has_tx():
        nonlocal txids
        txids = rpc_call(port, "getrawmempool")
        return len(txids) == 1

    wait_until(has_tx, timeout=20, interval=0.25, description="single mempool transaction")
    return txids[0]


def run_test(*, save_screenshots=False, screenshot_root=None):
    harness = WalletFlowHarness("qml_test_send_receive", port_offset=60)
    checkpoints = CheckpointRecorder(save_screenshots, screenshot_root)
    gui = None
    try:
        checkpoints.checkpoint("starting source node")
        harness.start_source_node()
        rpc_call(harness.source_rpc_port, "createwallet", {"wallet_name": RECEIVER_WALLET_NAME})
        receiver_address = rpc_call(
            harness.source_rpc_port,
            "getnewaddress",
            wallet=RECEIVER_WALLET_NAME,
        )
        checkpoints.checkpoint("receiver wallet prepared")

        harness.start_gui()
        gui = harness.driver
        gui.wait_for_property("walletBadge", "loading", False, timeout_ms=20000)
        gui.wait_for_property("walletBadge", "visible", True, timeout_ms=10000)
        assert gui.get_property("walletBadge", "noWalletLoaded") is True, "Expected no wallet at startup"
        checkpoints.checkpoint("gui launched", gui)

        rpc_call(harness.gui_rpc_port, "createwallet", {"wallet_name": GUI_WALLET_NAME})
        gui.wait_for_property("walletBadge", "text", GUI_WALLET_NAME, timeout_ms=20000)
        gui.wait_for_property("walletBadge", "noWalletLoaded", False, timeout_ms=10000)
        checkpoints.checkpoint("gui wallet created", gui)

        mining_address = rpc_call(
            harness.gui_rpc_port,
            "getnewaddress",
            wallet=GUI_WALLET_NAME,
        )
        rpc_call(harness.gui_rpc_port, "generatetoaddress", [101, mining_address])
        wait_for_wallet_balance(
            harness.gui_rpc_port,
            GUI_WALLET_NAME,
            minimum_balance=Decimal("50"),
        )
        checkpoints.checkpoint("gui wallet funded", gui)

        gui.click("desktopWalletsSendTab")
        gui.wait_for_page("sendPage", timeout_ms=10000)
        checkpoints.checkpoint("send page opened", gui)

        gui.set_text("sendAddressInput", receiver_address)
        gui.set_text("sendAmountInput", SEND_AMOUNT)
        gui.wait_for_property("sendReviewButton", "enabled", True, timeout_ms=20000)
        checkpoints.checkpoint("send form populated", gui)

        gui.wait_for_property("feeSelectionControl", "selectedLabel", DEFAULT_FEE_LABEL, timeout_ms=5000)
        gui.wait_for_property("feeSelectionControl", "selectedDuration", DEFAULT_FEE_DURATION, timeout_ms=5000)
        gui.wait_for_property("feeSelectionControl", "selectedTarget", 2, timeout_ms=5000)

        gui.click("feeSelectionDropdownButton")
        gui.wait_for_property("feeSelectionPopup", "opened", True, timeout_ms=5000)
        checkpoints.checkpoint("fee dropdown opened", gui)
        high_fee_option_estimate = wait_for_non_empty_text(gui, "feeSelectionOptionEstimate0")
        default_fee_option_estimate = wait_for_non_empty_text(gui, "feeSelectionOptionEstimate1")
        low_fee_option_estimate = wait_for_non_empty_text(gui, f"feeSelectionOptionEstimate{LOW_FEE_OPTION_INDEX}")
        assert high_fee_option_estimate, "Expected High option estimate to render in the dropdown"
        assert default_fee_option_estimate, "Expected Default option estimate to render in the dropdown"
        gui.click(f"feeSelectionOption{LOW_FEE_OPTION_INDEX}")
        gui.wait_for_property("feeSelectionPopup", "opened", False, timeout_ms=5000)
        gui.wait_for_property("feeSelectionControl", "selectedIndex", LOW_FEE_OPTION_INDEX, timeout_ms=5000)
        gui.wait_for_property("feeSelectionControl", "selectedLabel", LOW_FEE_LABEL, timeout_ms=5000)
        gui.wait_for_property("feeSelectionControl", "selectedDuration", LOW_FEE_DURATION, timeout_ms=5000)
        gui.wait_for_property("feeSelectionControl", "selectedTarget", LOW_FEE_TARGET_BLOCKS, timeout_ms=5000)
        gui.wait_for_property("feeSelectionEstimateLabel", "text", low_fee_option_estimate, timeout_ms=20000)
        checkpoints.checkpoint("low fee option selected", gui)

        estimated_fee_text = gui.get_text("feeSelectionEstimateLabel")
        estimated_fee_sats = btc_text_to_sats(estimated_fee_text)
        assert estimated_fee_sats > 0, f"Expected a positive estimated fee, got {estimated_fee_text!r}"

        gui.wait_for_property("feeSelectionControl", "includeFeeInAmount", False, timeout_ms=5000)
        gui.click("sendReviewButton")
        gui.wait_for_page("sendReviewPage", timeout_ms=10000)
        checkpoints.checkpoint("review page with include-fee off", gui)
        assert_review_values(
            gui,
            expected_fee_sats=estimated_fee_sats,
            expected_amount_sats=SEND_AMOUNT_SATS,
            expected_total_sats=SEND_AMOUNT_SATS + estimated_fee_sats,
        )

        gui.click("sendReviewBackButton")
        gui.wait_for_page("sendPage", timeout_ms=10000)
        gui.wait_for_property("sendReviewButton", "enabled", True, timeout_ms=10000)
        checkpoints.checkpoint("returned to send page", gui)

        gui.click("feeSelectionDropdownButton")
        gui.wait_for_property("feeSelectionPopup", "opened", True, timeout_ms=5000)
        checkpoints.checkpoint("fee dropdown reopened", gui)
        gui.click("feeSelectionIncludeFeeToggle")
        gui.wait_for_property("feeSelectionPopup", "opened", False, timeout_ms=5000)
        gui.wait_for_property("feeSelectionControl", "includeFeeInAmount", True, timeout_ms=5000)
        checkpoints.checkpoint("include fee in amount enabled", gui)

        gui.click("feeSelectionDropdownButton")
        gui.wait_for_property("feeSelectionPopup", "opened", True, timeout_ms=5000)
        checkpoints.checkpoint("fee dropdown with include fee enabled", gui)
        gui.click(f"feeSelectionOption{LOW_FEE_OPTION_INDEX}")
        gui.wait_for_property("feeSelectionPopup", "opened", False, timeout_ms=5000)
        gui.wait_for_property("feeSelectionControl", "selectedIndex", LOW_FEE_OPTION_INDEX, timeout_ms=5000)

        estimated_fee_with_subtract_text = gui.get_text("feeSelectionEstimateLabel")
        estimated_fee_with_subtract_sats = btc_text_to_sats(estimated_fee_with_subtract_text)
        assert estimated_fee_with_subtract_sats > 0, (
            f"Expected a positive estimated fee with subtract-fee enabled, got "
            f"{estimated_fee_with_subtract_text!r}"
        )

        gui.click("sendReviewButton")
        gui.wait_for_page("sendReviewPage", timeout_ms=10000)
        checkpoints.checkpoint("review page with include-fee on", gui)
        assert_review_values(
            gui,
            expected_fee_sats=estimated_fee_with_subtract_sats,
            expected_amount_sats=SEND_AMOUNT_SATS - estimated_fee_with_subtract_sats,
            expected_total_sats=SEND_AMOUNT_SATS,
        )

        gui.click("sendReviewSendButton")
        checkpoints.checkpoint("transaction submitted from review", gui)

        txid = wait_for_single_mempool_tx(harness.gui_rpc_port)
        tx_details = rpc_call(harness.gui_rpc_port, "gettransaction", [txid], wallet=GUI_WALLET_NAME)
        rpc_fee_sats = int(
            (
                abs(Decimal(str(tx_details["fee"]))) * Decimal("100000000")
            ).to_integral_value()
        )
        assert rpc_fee_sats == estimated_fee_with_subtract_sats, (
            f"Broadcast fee {rpc_fee_sats} sats did not match preview estimate "
            f"{estimated_fee_with_subtract_sats} sats"
        )
        checkpoints.checkpoint("broadcast fee verified", gui)

        print(
            "Send flow passed: preview totals were correct with include-fee off and on, "
            "and the broadcast fee matched the subtract-fee preview."
        )
        return 0
    except Exception as err:  # noqa: BLE001 - preserve failure context for functional test output
        print(f"\\nFAILED: {err}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        if gui is not None:
            try:
                checkpoints.checkpoint("failure state", gui)
            except Exception as screenshot_err:  # noqa: BLE001 - preserve original failure context
                print(f"[qml_test_send_receive] failed to save failure screenshot: {screenshot_err}", file=sys.stderr)
        gui_output = harness.process_output(harness.gui_process)
        if gui_output:
            print("\\n--- GUI process output ---", file=sys.stderr)
            print(gui_output, file=sys.stderr)
        if gui is not None:
            dump_qml_tree(gui)
        return 1
    finally:
        harness.stop()


if __name__ == "__main__":
    args = parse_args()
    screenshot_root = None
    if args.save_screenshots:
        screenshot_root = make_screenshot_root()
        print(f"Checkpoint screenshots will be saved under: {screenshot_root}")
    sys.exit(run_test(save_screenshots=args.save_screenshots, screenshot_root=screenshot_root))
