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
    review_amount_text = gui.get_property("sendTransactionReviewRecipient0", "amount")
    review_fee_text = gui.get_property("sendTransactionReviewFee", "value")
    review_total_text = gui.get_property("sendTransactionReviewTotal", "value")

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
    match = re.search(r"(-?[0-9]+(?:\.[0-9]+)?)", text.replace(",", ""))
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


def assert_no_fee_preview_label(port, wallet_name):
    labels = rpc_call(port, "listlabels", wallet=wallet_name)
    assert "qml-fee-preview" not in labels, f"Fee preview created address book label: {labels!r}"


def assert_receiver_output(port, txid, receiver_address, expected_sats):
    raw_tx = rpc_call(port, "getrawtransaction", [txid, True])
    matched_outputs = [
        vout for vout in raw_tx["vout"]
        if vout.get("scriptPubKey", {}).get("address") == receiver_address
    ]
    assert len(matched_outputs) == 1, (
        f"Expected one output to {receiver_address}, got {matched_outputs!r} "
        f"in decoded transaction {raw_tx!r}"
    )
    output_sats = int(
        (Decimal(str(matched_outputs[0]["value"])) * Decimal("100000000")).to_integral_value()
    )
    assert output_sats == expected_sats, (
        f"Expected receiver output {expected_sats} sats, got {output_sats} sats "
        f"in transaction {txid}"
    )
    return matched_outputs[0]["n"]


def enable_coin_control_and_select_first_coin(gui, checkpoints, coin_id=None):
    gui.click("sendCoinControlPickerOption_1")
    gui.click("sendSelectInputsButton")
    gui.wait_for_property("coinSelectionPopup", "opened", True, timeout_ms=10000)
    if coin_id:
        gui.set_text("coinBrowserSearch", coin_id)
    gui.click_list_item("coinSelectionListView", 0, "coinSelectionCheckbox")
    gui.wait_for_property("coinSelectionDoneButton", "enabled", True, timeout_ms=10000)
    checkpoints.checkpoint("one input selected", gui)
    gui.click("coinSelectionDoneButton")
    gui.wait_for_property("coinSelectionPopup", "visible", False, timeout_ms=10000)
    gui.wait_for_property("sendInputsSelectedText", "text", "1 input selected", timeout_ms=10000)


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
            ["Review input"],
            wallet=GUI_WALLET_NAME,
        )
        rpc_call(harness.gui_rpc_port, "generatetoaddress", [103, mining_address])
        wait_for_wallet_balance(
            harness.gui_rpc_port,
            GUI_WALLET_NAME,
            minimum_balance=Decimal("50"),
        )
        checkpoints.checkpoint("gui wallet funded", gui)

        gui.click("sendTabButton")
        gui.wait_for_page("sendPage", timeout_ms=10000)
        checkpoints.checkpoint("send page opened", gui)

        # Maximum can prefill the gross amount before an address is entered.
        gui.click("sendUseMaximumButton")
        gui.wait_for_property("sendAmountStatusText", "text", "Sending maximum available")
        gui.wait_for_property("sendUseMaximumButton", "enabled", False)
        wait_until(lambda: amount_text_to_sats(gui.get_text("sendAmountInput")) == 150 * 100000000,
                   description="maximum without an address")
        assert not gui.get_property("sendReviewButton", "enabled")
        gui.set_text("sendAddressInput", receiver_address)
        gui.wait_for_property("sendReviewButton", "enabled", True, timeout_ms=20000)
        initial_fee = amount_text_to_sats(gui.get_property("sendTotalFeesValue", "value"))
        assert amount_text_to_sats(gui.get_text("sendAmountInput")) == 150 * 100000000 - initial_fee
        # With no selection, maximum uses all spendable inputs without changing
        # the coin-control mode or action label, matching Core's sendall RPC.
        gui.wait_for_property("sendUseMaximumButton", "text", "Use maximum")
        gui.wait_for_property("sendReviewButton", "enabled", True, timeout_ms=20000)
        assert gui.get_property("sendCoinControlPicker", "currentIndex") == 0
        assert gui.get_property("sendUseMaximumButton", "text") == "Use maximum"
        all_fee = amount_text_to_sats(gui.get_property("sendTotalFeesValue", "value"))
        assert amount_text_to_sats(gui.get_text("sendAmountInput")) == 150 * 100000000 - all_fee
        utxos = rpc_call(harness.gui_rpc_port, "listunspent", wallet=GUI_WALLET_NAME)
        inputs = [{"txid": coin["txid"], "vout": coin["vout"]} for coin in utxos]
        reference = rpc_call(harness.gui_rpc_port, "sendall", {
            "recipients": [receiver_address], "fee_rate": 1,
            "options": {"inputs": inputs, "add_to_wallet": False},
        }, wallet=GUI_WALLET_NAME)
        reference_tx = rpc_call(harness.gui_rpc_port, "decoderawtransaction", [reference["hex"]])
        assert len(reference_tx["vin"]) == 3 and len(reference_tx["vout"]) == 1
        assert int(Decimal(str(reference_tx["vout"][0]["value"])) * 100000000) == 150 * 100000000 - all_fee
        assert rpc_call(harness.gui_rpc_port, "getrawmempool") == []
        checkpoints.checkpoint("automatic maximum preserves mode and matches sendall RPC", gui)
        # Same-wallet recipients skip the sweep warning. With the same inputs,
        # an external recipient must warn for both maximum actions, even with a lock.
        self_address = rpc_call(harness.gui_rpc_port, "getnewaddress", wallet=GUI_WALLET_NAME)
        rpc_call(harness.gui_rpc_port, "lockunspent", [False, [inputs[0]]], wallet=GUI_WALLET_NAME)
        for manual in (False, True):
            gui.set_text("sendAddressInput", self_address)
            if manual:
                gui.click("sendCoinControlPickerOption_1")
                gui.click("sendSelectInputsButton")
                gui.wait_for_property("coinSelectionPopup", "opened", True)
                for coin in inputs[1:]:
                    gui.set_text("coinBrowserSearch", f"{coin['txid']}:{coin['vout']}")
                    gui.click_list_item("coinSelectionListView", 0, "coinSelectionCheckbox")
                gui.set_text("coinBrowserSearch", "")
                gui.click("coinSelectionDoneButton")
                gui.wait_for_property("coinSelectionPopup", "visible", False)
            gui.wait_for_property("sendUseMaximumButton", "text",
                                  "Use maximum from selected coins" if manual else "Use maximum")
            gui.wait_for_property("sendAmountStatusText", "text",
                                  "Sending maximum from selected coins" if manual else "Sending maximum available")
            gui.wait_for_property("sendUseMaximumButton", "enabled", False)
            gui.wait_for_property("sendReviewButton", "enabled", True, timeout_ms=20000)
            gui.click("sendReviewButton")
            wait_until(lambda: gui.get_property("sendReviewSweepAlert", "opened")
                       or gui.get_property("transactionReviewPopup", "opened"),
                       description="self-send review or warning")
            assert not gui.get_property("sendReviewSweepAlert", "opened"), (
                f"Same-wallet recipient incorrectly triggered the warning (manual={manual})")
            gui.wait_for_page("sendTransactionReviewPage", timeout_ms=10000)
            gui.click("sendTransactionReviewCloseButton")
            gui.wait_for_property("transactionReviewPopup", "visible", False)
            gui.set_text("sendAddressInput", receiver_address)
            gui.wait_for_property("sendReviewButton", "enabled", True, timeout_ms=20000)
            gui.click("sendReviewButton")
            gui.wait_for_property("sendReviewSweepAlert", "opened", True)
            gui.click("sendReviewSweepConfirmButton")
            gui.wait_for_page("sendTransactionReviewPage", timeout_ms=10000)
            gui.click("sendTransactionReviewSendButton")
            gui.wait_for_property("sendSweepAlert", "opened", True)
            gui.click("sendSweepCancelButton")
            gui.wait_for_property("sendSweepAlert", "visible", False)
            gui.click("sendTransactionReviewCloseButton")
            gui.wait_for_property("transactionReviewPopup", "visible", False)
        assert rpc_call(harness.gui_rpc_port, "getrawmempool") == []
        gui.click("sendCoinControlPickerOption_0")
        rpc_call(harness.gui_rpc_port, "lockunspent", [True, [inputs[0]]], wallet=GUI_WALLET_NAME)
        gui.set_text("sendAddressInput", receiver_address)
        checkpoints.checkpoint("both maximum actions exempt self-sends and warn for external recipients", gui)
        # A smaller payment returns change and must open review without
        # claiming that the wallet will be swept.
        gui.set_text("sendAmountInput", SEND_AMOUNT)
        gui.wait_for_property("sendUseMaximumButton", "enabled", True)
        gui.wait_for_property("sendAmountStatusText", "visible", False)
        gui.wait_for_property("sendReviewButton", "enabled", True, timeout_ms=20000)
        gui.click("sendReviewButton")
        gui.wait_for_page("sendTransactionReviewPage", timeout_ms=10000)
        gui.wait_for_property("transactionFlowInput_0", "title", "Review input", timeout_ms=10000)
        assert gui.get_property("sendTransactionReviewTargetBlocks", "value") == "6 blocks"
        assert gui.get_property("sendTransactionReviewFeeRate", "value").endswith(" sat/vB")
        gui.click("sendTransactionReviewCloseButton")
        gui.wait_for_property("transactionReviewPopup", "visible", False)
        gui.click("sendUseMaximumButton")
        gui.wait_for_property("sendReviewButton", "enabled", True, timeout_ms=20000)
        second_address = rpc_call(harness.source_rpc_port, "getnewaddress", wallet=RECEIVER_WALLET_NAME)
        gui.click("sendAddRecipientButton")
        gui.set_text("sendAddressInput", second_address)
        gui.set_text("sendAmountInput", "2")
        gui.wait_for_property("sendReviewButton", "enabled", True, timeout_ms=20000)
        multiple_fee = amount_text_to_sats(gui.get_property("sendTotalFeesValue", "value"))
        gui.click("sendReviewButton")
        gui.wait_for_property("sendReviewSweepAlert", "opened", True)
        assert gui.get_property("sendReviewSweepAlert", "title") == "Use all available funds?"
        gui.click("sendReviewSweepCancelButton")
        gui.wait_for_property("sendReviewSweepAlert", "visible", False)
        assert gui.get_property("transactionReviewPopup", "opened") is False
        assert gui.get_property("sendCoinControlPicker", "currentIndex") == 0
        assert gui.get_property("sendUseMaximumButton", "text") == "Use maximum"
        assert amount_text_to_sats(gui.get_text("sendAmountInput")) == 2 * 100000000
        gui.click("sendReviewButton")
        gui.wait_for_property("sendReviewSweepAlert", "opened", True)
        gui.click("sendReviewSweepConfirmButton")
        gui.wait_for_property("sendReviewSweepAlert", "visible", False)
        gui.wait_for_page("sendTransactionReviewPage", timeout_ms=10000)
        assert amount_text_to_sats(gui.get_property("sendTransactionReviewRecipient0", "amount")) == 148 * 100000000 - multiple_fee
        assert amount_text_to_sats(gui.get_property("sendTransactionReviewRecipient1", "amount")) == 2 * 100000000
        assert amount_text_to_sats(gui.get_property("sendTransactionReviewTotal", "value")) == 150 * 100000000
        gui.click("sendTransactionReviewSendButton")
        gui.wait_for_property("sendSweepAlert", "opened", True)
        assert gui.get_property("sendSweepAlert", "title") == "Send all available funds?"
        assert rpc_call(harness.gui_rpc_port, "getrawmempool") == []
        gui.click("sendSweepCancelButton")
        gui.wait_for_property("sendSweepAlert", "visible", False)
        assert amount_text_to_sats(gui.get_property("sendTransactionReviewTotal", "value")) == 150 * 100000000
        gui.click("sendTransactionReviewCloseButton")
        gui.wait_for_property("transactionReviewPopup", "visible", False)
        gui.click("sendRemoveRecipient_1")
        checkpoints.checkpoint("maximum preserves the fixed amount of another recipient", gui)
        # Automatic maximum skips locked outputs, like Core's default sendall.
        gui.click("sendCoinControlPickerOption_0")
        rpc_call(harness.gui_rpc_port, "lockunspent", [False, [inputs[0]]], wallet=GUI_WALLET_NAME)
        gui.set_text("sendAddressInput", "")
        wait_until(lambda: amount_text_to_sats(gui.get_text("sendAmountInput")) == 100 * 100000000,
                   description="maximum before address excludes locked coins")
        gui.set_text("sendAddressInput", receiver_address)
        gui.wait_for_property("sendReviewButton", "enabled", True, timeout_ms=20000)
        assert gui.get_property("sendCoinControlPicker", "currentIndex") == 0
        assert gui.get_property("sendUseMaximumButton", "text") == "Use maximum"
        locked_fee = amount_text_to_sats(gui.get_property("sendTotalFeesValue", "value"))
        assert amount_text_to_sats(gui.get_text("sendAmountInput")) == 100 * 100000000 - locked_fee
        gui.click("sendReviewButton")
        gui.wait_for_property("sendReviewSweepAlert", "opened", True)
        gui.click("sendReviewSweepConfirmButton")
        gui.wait_for_page("sendTransactionReviewPage", timeout_ms=10000)
        gui.click("sendTransactionReviewSendButton")
        gui.wait_for_property("sendSweepAlert", "opened", True)
        gui.click("sendSweepCancelButton")
        gui.wait_for_property("sendSweepAlert", "visible", False)
        gui.click("sendTransactionReviewCloseButton")
        gui.wait_for_property("transactionReviewPopup", "visible", False)
        assert rpc_call(harness.gui_rpc_port, "getrawmempool") == []
        checkpoints.checkpoint("automatic maximum excludes locked coins and shows both warnings", gui)
        rpc_call(harness.gui_rpc_port, "lockunspent", [True, [inputs[0]]], wallet=GUI_WALLET_NAME)
        gui.click("sendCoinControlPickerOption_0")
        gui.set_text("sendAmountInput", "")
        # Select inputs before entering an amount.
        enable_coin_control_and_select_first_coin(gui, checkpoints)
        gui.set_text("sendAmountInput", SEND_AMOUNT)
        gui.wait_for_property("sendReviewButton", "enabled", True, timeout_ms=20000)
        checkpoints.checkpoint("send form populated", gui)
        assert_no_fee_preview_label(harness.gui_rpc_port, GUI_WALLET_NAME)

        gui.wait_for_property("feeSelectionControl", "currentTarget", 6, timeout_ms=5000)
        gui.click("feeSelectionPickerButton")
        gui.click("feeSelectionOption2")
        gui.wait_for_property("feeSelectionControl", "currentTarget", 10, timeout_ms=5000)
        gui.wait_for_property("sendReviewButton", "enabled", True, timeout_ms=20000)
        checkpoints.checkpoint("flexible fee selected", gui)
        estimated_fee_sats = amount_text_to_sats(gui.get_property("sendTotalFeesValue", "value"))
        assert estimated_fee_sats > 0

        gui.click("sendReviewButton")
        gui.wait_for_page("sendTransactionReviewPage", timeout_ms=10000)
        checkpoints.checkpoint("review page with fixed amount", gui)
        assert_review_values(
            gui,
            expected_fee_sats=estimated_fee_sats,
            expected_amount_sats=SEND_AMOUNT_SATS,
            expected_total_sats=SEND_AMOUNT_SATS + estimated_fee_sats,
        )

        gui.click("sendTransactionReviewCloseButton")
        gui.wait_for_page("sendPage", timeout_ms=10000)
        gui.wait_for_property("sendReviewButton", "enabled", True, timeout_ms=10000)
        checkpoints.checkpoint("returned to send page", gui)

        gui.wait_for_property("sendUseMaximumButton", "text", "Use maximum from selected coins")
        gui.click("sendUseMaximumButton")
        gui.wait_for_property("sendAmountStatusText", "text", "Sending maximum from selected coins")
        gui.wait_for_property("sendUseMaximumButton", "enabled", False)
        gui.wait_for_property("sendReviewButton", "enabled", True, timeout_ms=20000)
        checkpoints.checkpoint("maximum from selected input filled after fees", gui)
        gui.set_text("sendAddressInput", "")
        wait_until(lambda: amount_text_to_sats(gui.get_text("sendAmountInput")) == 50 * 100000000,
                   description="selected maximum without an address")
        assert not gui.get_property("sendReviewButton", "enabled")
        gui.set_text("sendAddressInput", receiver_address)
        gui.wait_for_property("sendReviewButton", "enabled", True, timeout_ms=20000)
        maximum_fee_sats = amount_text_to_sats(gui.get_property("sendTotalFeesValue", "value"))
        assert maximum_fee_sats > 0

        assert amount_text_to_sats(gui.get_text("sendAmountInput")) == 50 * 100000000 - maximum_fee_sats
        # Cancel must restore both the inputs and the maximum-send mode.
        gui.click("sendSelectInputsButton")
        gui.wait_for_property("coinSelectionPopup", "opened", True)
        gui.click_list_item("coinSelectionListView", 1, "coinSelectionCheckbox")
        gui.wait_for_property("sendInputsSelectedText", "text", "2 inputs selected")
        gui.click("coinSelectionCancelButton")
        gui.wait_for_property("coinSelectionPopup", "visible", False)
        gui.wait_for_property("sendInputsSelectedText", "text", "1 input selected")
        gui.wait_for_property("sendAmountStatusText", "text", "Sending maximum from selected coins")
        gui.wait_for_property("sendUseMaximumButton", "enabled", False)
        gui.wait_for_property("sendReviewButton", "enabled", True, timeout_ms=20000)
        # Repricing maximum must refresh the net amount without leaving maximum mode.
        gui.click("feeSelectionPickerButton")
        gui.click("feeSelectionOption3")
        gui.set_text("feeSelectionCustomRateInput", "2")
        gui.wait_for_property("sendReviewButton", "enabled", True, timeout_ms=20000)
        custom_fee = amount_text_to_sats(gui.get_property("sendTotalFeesValue", "value"))
        assert custom_fee == maximum_fee_sats * 2
        assert amount_text_to_sats(gui.get_text("sendAmountInput")) == 50 * 100000000 - custom_fee
        gui.click("feeSelectionPickerButton")
        gui.click("feeSelectionOption1")
        gui.wait_for_property("sendReviewButton", "enabled", True, timeout_ms=20000)
        assert amount_text_to_sats(gui.get_text("sendAmountInput")) == 50 * 100000000 - maximum_fee_sats
        # A manual amount edit leaves maximum mode; the fixed amount survives
        # a subsequent fee change.
        gui.set_text("sendAmountInput", SEND_AMOUNT)
        gui.wait_for_property("sendUseMaximumButton", "enabled", True)
        gui.wait_for_property("sendAmountStatusText", "visible", False)
        gui.click("feeSelectionPickerButton")
        gui.click("feeSelectionOption2")
        gui.wait_for_property("sendReviewButton", "enabled", True, timeout_ms=20000)
        assert amount_text_to_sats(gui.get_text("sendAmountInput")) == SEND_AMOUNT_SATS
        gui.click("sendUseMaximumButton")
        gui.wait_for_property("sendReviewButton", "enabled", True, timeout_ms=20000)
        gui.click("sendReviewButton")
        gui.wait_for_page("sendTransactionReviewPage", timeout_ms=10000)
        checkpoints.checkpoint("review page with sendall", gui)
        assert_review_values(
            gui,
            expected_fee_sats=maximum_fee_sats,
            expected_amount_sats=50 * 100000000 - maximum_fee_sats,
            expected_total_sats=50 * 100000000,
        )

        gui.click("sendTransactionReviewSendButton")
        checkpoints.checkpoint("transaction submitted from review", gui)

        txid = wait_for_single_mempool_tx(harness.gui_rpc_port)
        tx_details = rpc_call(harness.gui_rpc_port, "gettransaction", [txid], wallet=GUI_WALLET_NAME)
        rpc_fee_sats = int(
            (
                abs(Decimal(str(tx_details["fee"]))) * Decimal("100000000")
            ).to_integral_value()
        )
        assert rpc_fee_sats == maximum_fee_sats, (
            f"Broadcast fee {rpc_fee_sats} sats did not match preview estimate "
            f"{maximum_fee_sats} sats"
        )
        receiver_output_index = assert_receiver_output(
            harness.gui_rpc_port,
            txid,
            receiver_address,
            50 * 100000000 - maximum_fee_sats,
        )
        sent_tx = rpc_call(harness.gui_rpc_port, "decoderawtransaction", [tx_details["hex"]])
        assert len(sent_tx["vin"]) == 1 and len(sent_tx["vout"]) == 1, "sendall must have no change"
        checkpoints.checkpoint("broadcast fee verified", gui)

        gui.wait_for_page("sendCompletePage", timeout_ms=10000)
        gui.click("sendResultViewTransactionButton")
        gui.wait_for_page("activityDetailsPage", timeout_ms=10000)
        assert gui.get_property("activityDetailsPage", "txid") == txid
        assert gui.get_property("activityDetailsPage", "outputIndex") == -1
        gui.wait_for_property("activityDetailsPage", "detailsLoading", False, timeout_ms=10000)
        receiver_output = f"transactionFlowAddress_output:{receiver_output_index}"
        gui.wait_for_property(receiver_output, "address", receiver_address, timeout_ms=10000)
        assert gui.get_property(receiver_output, "visible") is True
        checkpoints.checkpoint("view transaction opens the sent activity", gui)
        gui.click("activityDetailsBackButton")
        gui.wait_for_property("activityStack", "depth", 1, timeout_ms=10000)
        gui.click("sendTabButton")
        gui.wait_for_page("sendPage", timeout_ms=10000)
        gui.wait_for_property("sendUseAutomaticInputsButton", "visible", False, timeout_ms=10000)
        checkpoints.checkpoint("coin control selection cleared after send", gui)
        gui.click("activityTabButton")
        gui.wait_for_property("activitySearchField", "visible", True, timeout_ms=10000)
        gui.click("activityTypeFilterButton")
        gui.click("activityTypeSent")
        gui.invoke("activityTypeFilterPopup", "close")
        gui.wait_for_property("activityFilterProxyModel", "count", 1, timeout_ms=20000)
        wait_until(
            lambda: gui.get_property(f"activityItem_{txid}", "amount") != "",
            timeout=10,
            description="sent Activity row delegate",
        )
        activity_amount_text = gui.get_property(f"activityItem_{txid}", "amount")
        activity_amount_sats = amount_text_to_sats(activity_amount_text)
        assert activity_amount_sats == 50 * 100000000, (
            f"Expected Activity to show the total sent magnitude, got {activity_amount_text!r}"
        )
        assert gui.get_property(f"activityItem_{txid}", "netAmountSat") == -50 * 100000000
        assert gui.get_property(f"activityItem_{txid}", "incoming") is False
        checkpoints.checkpoint("sent Activity amount and outgoing wallet impact verified", gui)

        # Manually select all available inputs with one other input locked,
        # then enter a fixed net amount without Use maximum. Both warnings apply.
        remaining = rpc_call(harness.gui_rpc_port, "listunspent", wallet=GUI_WALLET_NAME)
        locked_input = {"txid": remaining[0]["txid"], "vout": remaining[0]["vout"]}
        rpc_call(harness.gui_rpc_port, "lockunspent", [False, [locked_input]], wallet=GUI_WALLET_NAME)
        gui.click("sendTabButton")
        gui.wait_for_page("sendPage", timeout_ms=10000)
        gui.set_text("sendAddressInput", receiver_address)
        enable_coin_control_and_select_first_coin(gui, checkpoints,
                                                 f"{remaining[1]['txid']}:{remaining[1]['vout']}")
        remaining = remaining[1:]
        manual_reference = rpc_call(harness.gui_rpc_port, "sendall", {
            "recipients": [receiver_address], "fee_rate": 1,
            "options": {"inputs": [{"txid": coin["txid"], "vout": coin["vout"]} for coin in remaining],
                        "add_to_wallet": False},
        }, wallet=GUI_WALLET_NAME)
        manual_tx = rpc_call(harness.gui_rpc_port, "decoderawtransaction", [manual_reference["hex"]])
        # Fixed-amount construction reserves space for potential change during
        # selection. Leave 100 sats of headroom; this is below the change dust
        # threshold and is paid as fee, so the transaction still sweeps fully.
        manual_amount = Decimal(str(manual_tx["vout"][0]["value"])) - Decimal("0.00000100")
        review_password = "send-flow-review-test-password"
        rpc_call(harness.gui_rpc_port, "encryptwallet", [review_password], wallet=GUI_WALLET_NAME)
        gui.set_text("sendAmountInput", format(manual_amount, ".8f"))
        gui.wait_for_property("sendReviewButton", "enabled", True, timeout_ms=20000)
        gui.click("sendReviewButton")
        wait_until(lambda: gui.get_property("sendReviewSweepAlert", "opened")
                   or gui.get_property("transactionReviewPopup", "opened")
                   or gui.get_text("sendPrepareTransactionErrorText"), description="manual sweep preparation")
        assert gui.get_property("sendReviewSweepAlert", "opened"), (
            f"Manual sweep did not warn: error={gui.get_text('sendPrepareTransactionErrorText')!r}, "
            f"review={gui.get_property('transactionReviewPopup', 'opened')}, "
            f"amount={gui.get_text('sendAmountInput')}, fee={gui.get_property('sendTotalFeesValue', 'value')}"
        )
        assert rpc_call(harness.gui_rpc_port, "getrawmempool") == [txid]
        assert not gui.get_property("reviewPassphrasePopup", "visible")
        assert gui.get_property("sendReviewSweepConfirmButton", "text") == "Use maximum"
        gui.click("sendReviewSweepCancelButton")
        gui.wait_for_property("sendReviewSweepAlert", "visible", False)
        assert not gui.get_property("reviewPassphrasePopup", "visible")
        gui.click("sendReviewButton")
        gui.wait_for_property("sendReviewSweepAlert", "opened", True)
        gui.click("sendReviewSweepConfirmButton")
        gui.wait_for_property("sendReviewSweepAlert", "visible", False)
        gui.wait_for_property("reviewPassphrasePopup", "opened", True)
        gui.set_text("reviewPassphraseField", review_password)
        gui.click("reviewPassphraseConfirmButton")
        gui.wait_for_page("sendTransactionReviewPage", timeout_ms=10000)
        assert not gui.get_property("sendReviewSweepAlert", "visible")
        gui.click("sendTransactionReviewSendButton")
        gui.wait_for_property("sendSweepAlert", "opened", True)
        assert rpc_call(harness.gui_rpc_port, "getrawmempool") == [txid]
        gui.click("sendSweepConfirmButton")
        gui.wait_for_page("sendCompletePage", timeout_ms=10000)
        wait_until(lambda: len(rpc_call(harness.gui_rpc_port, "getrawmempool")) == 2,
                   description="confirmed sweep broadcast")
        assert rpc_call(harness.gui_rpc_port, "getbalance", wallet=GUI_WALLET_NAME) == 50
        assert rpc_call(harness.gui_rpc_port, "listlockunspent", wallet=GUI_WALLET_NAME) == [locked_input]
        checkpoints.checkpoint("sweep warning precedes passphrase and final send confirmation", gui)

        print(
            "Send flow passed: preview totals were correct for fixed amounts and sendall, "
            "the broadcast fee matched the sendall preview, and Activity showed "
            "the outgoing amount with the correct wallet impact."
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
