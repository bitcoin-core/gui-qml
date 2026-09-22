#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Functional coverage for send review page formatting and layout hooks."""

import argparse
import os
import re
import sys
import time
from datetime import datetime

from qml_driver import QmlDriverError
from qml_test_harness import dump_qml_tree
from qml_wallet_test_lib import WalletFlowHarness, rpc_call


def parse_args():
    parser = argparse.ArgumentParser(
        description="Send review GUI functional test",
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
    screenshot_root = os.path.join(artifacts_root, f"qml_test_send_review-{timestamp}")
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


def format_short_address(address):
    if len(address) > 8:
        return f"{address[:4]} {address[4:8]} ... {address[-8:-4]} {address[-4:]}"
    return address


def format_full_address(address):
    return " ".join(address[i:i + 4] for i in range(0, len(address), 4))


def wait_until(predicate, timeout=20, interval=0.1, description="condition"):
    deadline = time.time() + timeout
    last_error = None
    while time.time() < deadline:
        try:
            if predicate():
                return
        except Exception as err:  # noqa: BLE001 - test polling should tolerate transient UI state
            last_error = err
        time.sleep(interval)
    if last_error:
        raise AssertionError(f"Timed out waiting for {description}: {last_error}")
    raise AssertionError(f"Timed out waiting for {description}")


def create_wallet(gui, wallet_name):
    try:
        gui.wait_for_property("createWalletButton", "visible", True, timeout_ms=1000)
        gui.click("createWalletButton")
    except QmlDriverError:
        pass
    gui.wait_for_property("walletTypeRegular", "visible", True, timeout_ms=5000)
    gui.click("walletTypeRegular")
    gui.wait_for_page("createWalletIntroPage", timeout_ms=10000)
    gui.click("createWalletIntroStartButton")
    gui.wait_for_page("createWalletNamePage", timeout_ms=10000)
    gui.set_text("createWalletNameInput", wallet_name)
    gui.click("createWalletNameContinueButton")
    gui.wait_for_page("createWalletPasswordPage", timeout_ms=10000)
    gui.wait_for_property("createWalletPasswordSkipButton", "enabled", True, timeout_ms=30000)
    gui.click("createWalletPasswordSkipButton")
    gui.wait_for_page("createWalletConfirmPage", timeout_ms=10000)
    gui.click("createWalletConfirmNextButton")
    gui.wait_for_page("createWalletBackupPage", timeout_ms=10000)
    gui.click("createWalletBackupDoneButton")
    gui.wait_for_property("walletBadge", "text", wallet_name, timeout_ms=20000)
    gui.settle(timeout_ms=10000)


def fund_wallet(harness, wallet_name):
    mining_address = rpc_call(harness.gui_rpc_port, "getnewaddress", wallet=wallet_name)
    rpc_call(harness.gui_rpc_port, "generatetoaddress", [101, mining_address])
    wait_until(
        lambda: float(rpc_call(harness.gui_rpc_port, "getbalance", wallet=wallet_name)) > 0,
        description="wallet RPC balance",
    )


def wallet_txcount(harness, wallet_name):
    return rpc_call(harness.gui_rpc_port, "getwalletinfo", wallet=wallet_name)["txcount"]


def assert_review_has_no_transaction_side_effects(harness, wallet_name, expected_txcount):
    mempool_txids = rpc_call(harness.gui_rpc_port, "getrawmempool")
    assert mempool_txids == [], f"Review-only flows should not broadcast transactions, got {mempool_txids}"
    actual_txcount = wallet_txcount(harness, wallet_name)
    assert actual_txcount == expected_txcount, (
        f"Review-only flows should not add wallet transactions, "
        f"expected txcount {expected_txcount}, got {actual_txcount}"
    )


def open_send_page(gui):
    gui.wait_for_property("sendTabButton", "visible", True, timeout_ms=10000)
    gui.wait_for_property("sendTabButton", "enabled", True, timeout_ms=10000)
    gui.click("sendTabButton")
    gui.wait_for_page("sendPage", timeout_ms=10000)
    wait_for_send_form(gui)


def wait_for_send_form(gui):
    gui.wait_for_property("sendPage", "busy", False, timeout_ms=10000)
    gui.wait_for_property("sendPage", "depth", 1, timeout_ms=10000)
    gui.wait_for_property("sendOptionsButton", "visible", True, timeout_ms=10000)
    gui.wait_for_property("sendOptionsButton", "enabled", True, timeout_ms=10000)


def open_send_options(gui):
    wait_for_send_form(gui)
    if gui.get_property("sendOptionsPopup", "opened") is True:
        return
    gui.click("sendOptionsButton")
    try:
        gui.wait_for_property("sendOptionsPopup", "opened", True, timeout_ms=5000)
    except QmlDriverError as err:
        page = gui.get_current_page()
        depth = gui.get_property("sendPage", "depth")
        busy = gui.get_property("sendPage", "busy")
        button_visible = gui.get_property("sendOptionsButton", "visible")
        button_enabled = gui.get_property("sendOptionsButton", "enabled")
        raise AssertionError(
            "Send options popup did not open "
            f"(page={page!r}, sendPage.depth={depth!r}, sendPage.busy={busy!r}, "
            f"sendOptionsButton.visible={button_visible!r}, "
            f"sendOptionsButton.enabled={button_enabled!r})"
        ) from err


def close_send_options(gui):
    if gui.get_property("sendOptionsPopup", "opened") is not True:
        return
    gui.click("sendOptionsButton")
    gui.wait_for_property("sendOptionsPopup", "opened", False, timeout_ms=5000)


def set_multiple_recipients(gui, enabled):
    if enabled:
        if not gui.object_exists("sendRecipientCard_1"):
            gui.click("sendAddRecipientButton")
    else:
        while gui.object_exists("sendRecipientCard_1"):
            gui.click("sendRemoveRecipient_1")
    gui.settle()


def set_amount_unit(gui, unit_label):
    def matches(label):
        if unit_label in ("sat", "sats"):
            return label in ("sat", "sats")
        return label == unit_label

    current = gui.get_property("sendAmountUnitLabel", "text")
    if not matches(current):
        gui.click("sendAmountUnitToggle")
        gui.wait_for_property("sendAmountUnitLabel", "text", matches, timeout_ms=5000)


def prepare_single_send(gui, address, amount, amount_unit="btc"):
    open_send_page(gui)
    set_multiple_recipients(gui, False)
    set_amount_unit(gui, "sat" if amount_unit == "sat" else "₿")
    gui.set_text("sendAddressInput", address)
    gui.set_text("sendAmountInput", amount)
    gui.wait_for_property("sendReviewButton", "enabled", True, timeout_ms=10000)
    gui.click("sendReviewButton")
    gui.wait_for_page("sendReviewPage", timeout_ms=10000)
    gui.wait_for_property("sendReviewSendButton", "visible", True, timeout_ms=10000)


def prepare_multi_send(gui, first_address, first_amount_btc, second_address, second_amount_btc):
    open_send_page(gui)
    set_multiple_recipients(gui, True)

    set_amount_unit(gui, "₿")
    gui.set_text("sendAddressInput", second_address)
    gui.set_text("sendAmountInput", second_amount_btc)
    gui.click("sendEditRecipient_0")
    gui.set_text("sendAddressInput", first_address)
    gui.set_text("sendAmountInput", first_amount_btc)
    gui.set_text("sendNoteInput", "Alice's salary")
    gui.click("sendEditRecipient_1")
    set_amount_unit(gui, "sat")
    gui.wait_for_property("sendReviewButton", "enabled", True, timeout_ms=10000)
    gui.click("sendReviewButton")
    gui.wait_for_page("sendReviewPage", timeout_ms=10000)
    gui.wait_for_property("multipleRecipientsSummary", "visible", True, timeout_ms=10000)


def wait_for_send_error(gui, expected_text):
    gui.wait_for_property("sendPrepareTransactionErrorText", "text", expected_text, timeout_ms=10000)
    assert gui.get_property("sendReviewButton", "enabled") is False


def assert_unit_suffix(gui, object_name, unit_suffix):
    text = gui.get_text(object_name)
    if unit_suffix in ("sat", "sats"):
        assert text.endswith(" sat") or text.endswith(" sats"), (
            f"Expected {object_name} to end with a satoshi unit, got {text!r}"
        )
    else:
        assert text.endswith(f" {unit_suffix}"), f"Expected {object_name} to end with {unit_suffix!r}, got {text!r}"


def assert_amount_text(gui, object_name, expected_amount, expected_unit):
    text = gui.get_text(object_name)
    if expected_unit in ("sat", "sats"):
        expected_values = {f"{expected_amount} sat", f"{expected_amount} sats"}
        assert text in expected_values, f"Expected {object_name} to be one of {expected_values!r}, got {text!r}"
    else:
        expected = f"{expected_amount} {expected_unit}"
        assert text == expected, f"Expected {object_name} to be {expected!r}, got {text!r}"


def assert_save_psbt(gui, destination):
    gui.set_text("sendReviewSavePsbtPathField", destination)
    gui.click("sendReviewSavePsbtButton")
    gui.wait_for_property("sendReviewSavePsbtStatus", "text", "Saved.", timeout_ms=10000)
    gui.wait_for_property("sendReviewPage", "savePsbtError", False, timeout_ms=10000)

    with open(destination, "rb") as psbt_file:
        assert psbt_file.read(5) == b"psbt\xff", "Saved transaction is not a raw PSBT file"


def assert_save_psbt_error(gui, destination):
    gui.set_text("sendReviewSavePsbtPathField", destination)
    gui.click("sendReviewSavePsbtButton")
    gui.wait_for_property("sendReviewPage", "savePsbtError", True, timeout_ms=10000)
    status = gui.get_text("sendReviewSavePsbtStatus")
    assert status.startswith("Could not save PSBT:"), f"Unexpected PSBT save error: {status!r}"


def assert_address_toggle(gui, object_name, address, checkpoints=None, label=None):
    wait_until(
        lambda: gui.get_property(object_name, "displayText") == format_short_address(address),
        description=f"{object_name} truncated text",
    )
    assert gui.get_property(object_name, "expanded") is False

    gui.click(object_name)
    gui.wait_for_property(object_name, "expanded", True, timeout_ms=5000)
    if checkpoints and label:
        checkpoints.checkpoint(f"{label} address expanded", gui)
    assert gui.get_property(object_name, "displayText") == format_full_address(address)

    gui.click(object_name)
    gui.wait_for_property(object_name, "expanded", False, timeout_ms=5000)
    assert gui.get_property(object_name, "displayText") == format_short_address(address)


def return_to_send_page(gui, back_button):
    gui.click(back_button)
    gui.wait_for_page("sendPage", timeout_ms=10000)
    wait_for_send_form(gui)


def case_single_btc(harness, gui, wallet_name, checkpoints):
    recipient_address = rpc_call(harness.gui_rpc_port, "getnewaddress", wallet=wallet_name)
    prepare_single_send(gui, recipient_address, "1.25000000", amount_unit="btc")
    checkpoints.checkpoint("single-btc review page displayed", gui)

    assert_address_toggle(
        gui,
        "sendReviewAddressField",
        recipient_address,
        checkpoints=checkpoints,
        label="single-btc review",
    )
    assert_amount_text(gui, "sendReviewAmountField", "1.25000000", "₿")
    assert_unit_suffix(gui, "sendReviewFeeField", "₿")
    assert_unit_suffix(gui, "sendReviewTotalField", "₿")
    saved_psbt = os.path.join(harness.tmpdir, "review-transaction.psbt")
    assert_save_psbt(gui, saved_psbt)
    assert_save_psbt_error(gui, os.path.join(harness.tmpdir, "missing", "transaction.psbt"))
    checkpoints.checkpoint("single-btc PSBT save success and failure displayed", gui)
    return_to_send_page(gui, "sendReviewBackButton")


def case_single_sat(harness, gui, wallet_name, checkpoints):
    recipient_address = rpc_call(harness.gui_rpc_port, "getnewaddress", wallet=wallet_name)
    prepare_single_send(gui, recipient_address, "1250", amount_unit="sat")
    checkpoints.checkpoint("single-sat review page displayed", gui)

    assert_address_toggle(
        gui,
        "sendReviewAddressField",
        recipient_address,
        checkpoints=checkpoints,
        label="single-sat review",
    )
    assert_amount_text(gui, "sendReviewAmountField", "1250", "sat")
    assert_unit_suffix(gui, "sendReviewFeeField", "sat")
    assert_unit_suffix(gui, "sendReviewTotalField", "sat")
    return_to_send_page(gui, "sendReviewBackButton")


def case_invalid_recipients(harness, gui, wallet_name, checkpoints):
    first_address = rpc_call(harness.gui_rpc_port, "getnewaddress", wallet=wallet_name)
    second_address = rpc_call(harness.gui_rpc_port, "getnewaddress", wallet=wallet_name)

    open_send_page(gui)
    set_multiple_recipients(gui, False)
    set_amount_unit(gui, "sat")
    gui.set_text("sendAddressInput", first_address)
    gui.set_text("sendAmountInput", "1")
    gui.wait_for_property("sendAmountErrorText", "text", "Amount is too small to send.", timeout_ms=10000)
    assert gui.get_property("sendReviewButton", "enabled") is False
    checkpoints.checkpoint("dust amount rejected", gui)

    set_multiple_recipients(gui, True)
    gui.click("sendEditRecipient_0")
    set_amount_unit(gui, "₿")
    gui.set_text("sendAddressInput", first_address)
    gui.set_text("sendAmountInput", "0.50000000")
    wait_for_send_error(gui, "Complete every recipient before continuing.")
    checkpoints.checkpoint("empty second recipient rejected", gui)

    gui.click("sendEditRecipient_1")
    set_amount_unit(gui, "₿")
    gui.set_text("sendAddressInput", first_address)
    gui.set_text("sendAmountInput", "0.25000000")
    wait_for_send_error(gui, "Recipient addresses must be unique.")
    checkpoints.checkpoint("duplicate recipient rejected", gui)

    gui.set_text("sendAddressInput", second_address)
    gui.wait_for_property("sendReviewButton", "enabled", True, timeout_ms=10000)
    set_multiple_recipients(gui, False)


def case_multi_review(harness, gui, wallet_name, checkpoints):
    first_address = rpc_call(harness.gui_rpc_port, "getnewaddress", wallet=wallet_name)
    second_address = rpc_call(harness.gui_rpc_port, "getnewaddress", wallet=wallet_name)
    prepare_multi_send(
        gui,
        first_address=first_address,
        first_amount_btc="0.50000000",
        second_address=second_address,
        second_amount_btc="0.00002000",
    )
    checkpoints.checkpoint("multi review page displayed", gui)

    assert gui.get_text("sendReviewRecipientCountText") == "There are 2 recipients."
    assert gui.get_list_item_property(
        view_object_name="multipleSendReviewRecipientsList",
        row_index=0,
        prop="address",
    ) == format_short_address(first_address)
    assert gui.get_list_item_property(
        view_object_name="multipleSendReviewRecipientsList",
        row_index=0,
        prop="formattedAddress",
    ) == format_full_address(first_address)
    assert gui.get_list_item_property(
        view_object_name="multipleSendReviewRecipientsList",
        row_index=0,
        prop="addressText",
    ) == format_short_address(first_address)
    assert gui.get_list_item_property(
        view_object_name="multipleSendReviewRecipientsList",
        row_index=0,
        prop="amountText",
    ) == "50000000 sats"
    assert gui.get_list_item_property(
        view_object_name="multipleSendReviewRecipientsList",
        row_index=1,
        prop="address",
    ) == format_short_address(second_address)
    assert gui.get_list_item_property(
        view_object_name="multipleSendReviewRecipientsList",
        row_index=1,
        prop="formattedAddress",
    ) == format_full_address(second_address)
    assert gui.get_list_item_property(
        view_object_name="multipleSendReviewRecipientsList",
        row_index=1,
        prop="amountText",
    ) == "2000 sats"
    assert gui.get_list_item_property(
        view_object_name="multipleSendReviewRecipientsList",
        row_index=1,
        prop="addressText",
    ) == format_short_address(second_address)
    gui.click_list_item(
        view_object_name="multipleSendReviewRecipientsList",
        row_index=1,
    )
    wait_until(
        lambda: gui.get_list_item_property(
            view_object_name="multipleSendReviewRecipientsList",
            row_index=1,
            prop="expanded",
        ) is True,
        description="multiple recipient row expanded",
    )
    assert gui.get_list_item_property(
        view_object_name="multipleSendReviewRecipientsList",
        row_index=1,
        prop="addressText",
    ) == format_full_address(second_address)
    checkpoints.checkpoint("multi review recipient expanded", gui)
    gui.click_list_item(
        view_object_name="multipleSendReviewRecipientsList",
        row_index=1,
    )
    wait_until(
        lambda: gui.get_list_item_property(
            view_object_name="multipleSendReviewRecipientsList",
            row_index=1,
            prop="expanded",
        ) is False,
        description="multiple recipient row collapsed",
    )
    assert gui.get_list_item_property(
        view_object_name="multipleSendReviewRecipientsList",
        row_index=1,
        prop="addressText",
    ) == format_short_address(second_address)
    assert_unit_suffix(gui, "multipleSendReviewFeeField", "sats")
    assert_unit_suffix(gui, "multipleSendReviewTotalField", "sats")
    return_to_send_page(gui, "sendReviewBackButton")


def run_tests(args):
    screenshot_root = None
    if args.save_screenshots:
        screenshot_root = make_screenshot_root()
        print(f"Checkpoint screenshots will be saved under: {screenshot_root}")
    case_name = "qml_send_review"
    harness = WalletFlowHarness(case_name, port_offset=70)
    checkpoints = CheckpointRecorder(case_name, args.save_screenshots, screenshot_root)
    gui = None
    try:
        print(f"[{case_name}] starting")
        wallet_name = "send_review"
        harness.start_gui()
        gui = harness.driver
        checkpoints.checkpoint("GUI launched", gui)
        harness.finish_onboarding()
        checkpoints.checkpoint("onboarding completed", gui)
        create_wallet(gui, wallet_name)
        checkpoints.checkpoint("wallet created", gui)
        fund_wallet(harness, wallet_name)
        funded_txcount = wallet_txcount(harness, wallet_name)
        checkpoints.checkpoint("wallet funded", gui)

        case_invalid_recipients(harness, gui, wallet_name, checkpoints)
        case_single_btc(harness, gui, wallet_name, checkpoints)
        case_single_sat(harness, gui, wallet_name, checkpoints)
        case_multi_review(harness, gui, wallet_name, checkpoints)
        assert_review_has_no_transaction_side_effects(harness, wallet_name, funded_txcount)

        print(f"[{case_name}] completed")
        print("Send review flows passed.")
    except Exception as err:  # noqa: BLE001 - preserve failure context for functional test output
        print(f"\nFAILED [{case_name}]: {err}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        if gui is not None:
            try:
                checkpoints.checkpoint("failure state", gui)
            except Exception as screenshot_err:  # noqa: BLE001 - preserve original failure context
                print(f"[{case_name}] failed to save failure screenshot: {screenshot_err}", file=sys.stderr)
        gui_output = harness.process_output(harness.gui_process)
        if gui_output:
            print("\n--- GUI process output ---", file=sys.stderr)
            print(gui_output, file=sys.stderr)
        if gui is not None:
            dump_qml_tree(gui)
        raise SystemExit(1)
    finally:
        harness.stop()


if __name__ == "__main__":
    try:
        run_tests(parse_args())
    except Exception:
        sys.exit(1)
