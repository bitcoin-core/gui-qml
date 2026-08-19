#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""End-to-end GUI coverage for the external signer send flow."""

import argparse
import json
import os
import re
import sys
import time
from datetime import datetime

from qml_driver import QmlDriverError
from qml_test_harness import dump_qml_tree
from qml_wallet_test_lib import WalletFlowHarness, rpc_call, wait_for_rpc


EXPECTED_SIGNER_NAME = "trezor_t"
EXPECTED_STATUS_TEXT = f"Detected external signer: {EXPECTED_SIGNER_NAME}"
EXPECTED_FIRST_BECH32_ADDRESS = "bcrt1qm90ugl4d48jv8n6e5t9ln6t9zlpm5th68x4f8g"
EXPECTED_FIRST_BECH32M_ADDRESS = "bcrt1phw4cgpt6cd30kz9k4wkpwm872cdvhss29jga2xpmftelhqll62ms4e9sqj"
EXPECTED_FIRST_HD_KEYPATH = "m/84h/1h/0h/0/0"
EXPECTED_FIRST_TAPROOT_HD_KEYPATH = "m/86h/1h/0h/0/0"


def parse_args():
    parser = argparse.ArgumentParser(
        description="External signer GUI functional test",
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
    screenshot_root = os.path.join(artifacts_root, f"qml_test_external_signer-{timestamp}")
    os.makedirs(screenshot_root, exist_ok=True)
    return screenshot_root


def find_mock_signer_path(mock_name="signer"):
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
    signer_path = os.path.join(repo_root, "bitcoin", "test", "functional", "mocks", f"{mock_name}.py")
    if not os.path.isfile(signer_path):
        raise FileNotFoundError(f"Mock signer not found at {signer_path}")
    if not os.access(signer_path, os.X_OK):
        raise PermissionError(f"Mock signer is not executable: {signer_path}")
    return signer_path


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
        os.makedirs(case_dir, exist_ok=True)
        filename = f"{self.index:02d}-{self._sanitize_label(label)}.png"
        screenshot_path = os.path.join(case_dir, filename)
        screenshot = gui.save_screenshot(screenshot_path)
        print(
            f"{prefix}: screenshot saved to {screenshot['path']} "
            f"({screenshot['width']}x{screenshot['height']})"
        )


def wait_until(predicate, timeout=20, interval=0.1, description="condition"):
    deadline = time.time() + timeout
    last_error = None
    while time.time() < deadline:
        try:
            if predicate():
                return
        except Exception as err:  # noqa: BLE001 - test polling should tolerate transient state
            last_error = err
        time.sleep(interval)
    if last_error:
        raise AssertionError(f"Timed out waiting for {description}: {last_error}")
    raise AssertionError(f"Timed out waiting for {description}")


def expect_runtime_error(callable_obj, expected_substring):
    try:
        callable_obj()
    except RuntimeError as err:
        assert expected_substring in str(err), (
            f"Expected error containing {expected_substring!r}, got {err!r}"
        )
        return
    raise AssertionError(f"Expected RuntimeError containing {expected_substring!r}")


def create_wallet(port, name, *, disable_private_keys=False, blank=False, load_on_startup=False, external_signer=False):
    return rpc_call(
        port,
        "createwallet",
        [
            name,
            disable_private_keys,
            blank,
            "",
            False,
            True,
            load_on_startup,
            external_signer,
        ],
    )


def load_settings(settings_path):
    with open(settings_path, "r", encoding="utf8") as settings_file:
        return json.load(settings_file)


def wait_for_wallet(gui, wallet_name):
    gui.wait_for_property("walletBadge", "text", wallet_name, timeout_ms=20000)
    gui.settle(timeout_ms=5000)


def wait_for_wallet_balance(port, wallet_name):
    wait_until(
        lambda: float(rpc_call(port, "getbalance", wallet=wallet_name)) > 0,
        timeout=30,
        description=f"{wallet_name} balance",
    )


def wait_for_wallet_badge_balance(gui, expected_balance):
    wait_until(
        lambda: gui.get_property("walletBadge", "balance") == expected_balance,
        timeout=30,
        description=f"wallet badge balance {expected_balance}",
    )


def open_add_wallet_flow(gui):
    gui.wait_for_property("walletBadge", "loading", False, timeout_ms=20000)
    gui.click("walletBadge")
    try:
        gui.wait_for_property("walletSelectPopup", "opened", True, timeout_ms=2000)
        gui.click("walletSelectAddWalletButton")
    except QmlDriverError:
        pass
    gui.wait_for_property("walletTypeExternalSigner", "visible", True, timeout_ms=10000)


def select_wallet(gui, wallet_name):
    gui.wait_for_property("walletBadge", "loading", False, timeout_ms=20000)
    if gui.get_property("walletBadge", "text") == wallet_name:
        return

    gui.click("walletBadge")
    gui.wait_for_property("walletSelectPopup", "opened", True, timeout_ms=5000)

    def read_wallet_picker_rows():
        wallet_count = gui.get_property("walletSelectList", "count")
        rows = []
        for row_index in range(wallet_count):
            try:
                rows.append(
                    gui.get_list_item_property(
                        view_object_name="walletSelectList",
                        row_index=row_index,
                        prop="name",
                    )
                )
            except QmlDriverError:
                return None
        return rows

    picker_state = {}

    def wallet_picker_ready():
        rows = read_wallet_picker_rows()
        if rows is None:
            return False
        picker_state["rows"] = rows
        return True

    wait_until(wallet_picker_ready, timeout=10, description="wallet picker rows")

    for row_index, row_wallet_name in enumerate(picker_state["rows"]):
        if row_wallet_name == wallet_name:
            gui.click_list_item(
                view_object_name="walletSelectList",
                row_index=row_index,
            )
            gui.wait_for_property("walletBadge", "text", wallet_name, timeout_ms=10000)
            gui.wait_for_property("walletSelectPopup", "opened", False, timeout_ms=5000)
            gui.settle(timeout_ms=5000)
            return

    raise AssertionError(f"Wallet {wallet_name!r} not found in wallet picker")


def ensure_desktop_wallets_visible(gui):
    try:
        gui.wait_for_property("desktopWalletSettingsTabButton", "visible", True, timeout_ms=1000)
        return
    except QmlDriverError:
        pass

    try:
        gui.wait_for_property("createWalletWizardExitButton", "visible", True, timeout_ms=1000)
        gui.click("createWalletWizardExitButton")
    except QmlDriverError:
        gui.wait_for_property("typeSelectorCancelButton", "visible", True, timeout_ms=10000)
        gui.click("typeSelectorCancelButton")
    gui.wait_for_property("desktopWalletSettingsTabButton", "visible", True, timeout_ms=10000)


def open_wallet_settings(gui):
    ensure_desktop_wallets_visible(gui)
    gui.click("desktopWalletSettingsTabButton")
    # External-signer configuration lives in its own redesigned sidebar page.
    gui.wait_for_property("settingsSidebar_external-signer", "visible", True, timeout_ms=10000)
    gui.click("settingsSidebar_external-signer")
    gui.wait_for_property("externalSignerPathInput", "visible", True, timeout_ms=10000)


def open_selected_wallet_settings(gui):
    ensure_desktop_wallets_visible(gui)
    gui.click("desktopWalletSettingsTabButton")
    # Per-wallet settings live under the sidebar Wallet section.
    gui.wait_for_property("settingsSidebar_wallet", "visible", True, timeout_ms=10000)
    gui.click("settingsSidebar_wallet")
    try:
        gui.wait_for_page("walletSettingsPage", timeout_ms=1000)
    except QmlDriverError:
        # If a prior step left the preserved wallet stack on a subpage, return
        # to the redesigned wallet settings root.
        for back_button in (
            "walletPasswordBackButton",
            "addressListBackButton",
            "signVerifyMessageBackButton",
        ):
            try:
                if gui.get_property(back_button, "visible") is True:
                    gui.click(back_button)
                    break
            except QmlDriverError:
                pass
        gui.wait_for_page("walletSettingsPage", timeout_ms=10000)


def configure_external_signer_via_gui(harness, checkpoints, signer_path):
    gui = harness.driver
    open_wallet_settings(gui)
    checkpoints.checkpoint("wallet settings opened", gui)

    gui.set_text("externalSignerPathInput", signer_path)
    checkpoints.checkpoint("mock signer path populated", gui)

    gui.click("externalSignerCheckDeviceButton")
    gui.wait_for_property("externalSignerStatusText", "text", EXPECTED_STATUS_TEXT, timeout_ms=10000)
    wait_until(
        lambda: os.path.exists(harness.gui_settings_path),
        description="settings.json creation",
    )
    wait_until(
        lambda: load_settings(harness.gui_settings_path).get("signer") == signer_path,
        description="signer setting persistence",
    )
    checkpoints.checkpoint("mock signer detected", gui)
    print(f"[{checkpoints.case_name}] signer status: {gui.get_text('externalSignerStatusText')}")


def create_and_verify_external_wallet(harness, checkpoints):
    gui = harness.driver
    open_add_wallet_flow(gui)
    checkpoints.checkpoint("add wallet flow opened", gui)

    gui.wait_for_property("walletTypeExternalSigner", "visible", True, timeout_ms=10000)
    checkpoints.checkpoint("external wallet option available", gui)

    gui.click("walletTypeExternalSigner")
    gui.wait_for_property("externalWalletNameInput", "visible", True, timeout_ms=10000)
    gui.wait_for_property("createExternalWalletButton", "enabled", True, timeout_ms=10000)
    checkpoints.checkpoint("external wallet form opened", gui)

    wallet_name = gui.get_text("externalWalletNameInput")
    print(f"[{checkpoints.case_name}] suggested wallet name: {wallet_name}")
    assert wallet_name == EXPECTED_SIGNER_NAME, (
        f"Expected wallet name to default to {EXPECTED_SIGNER_NAME!r}, got {wallet_name!r}"
    )
    checkpoints.checkpoint("wallet name prefilled from signer", gui)

    gui.click("createExternalWalletButton")
    gui.wait_for_page("externalWalletCreatedPage", timeout_ms=20000)
    checkpoints.checkpoint("external wallet created", gui)

    gui.click("externalWalletCreatedDoneButton")
    gui.wait_for_property("walletBadge", "text", wallet_name, timeout_ms=20000)
    checkpoints.checkpoint("wallet overview reopened", gui)

    wallet_dir = os.path.join(harness.gui_wallets_path, wallet_name)
    assert os.path.isdir(wallet_dir), f"Expected wallet directory at {wallet_dir}"
    print(f"[{checkpoints.case_name}] wallet directory: {wallet_dir}")

    wallet_info = rpc_call(harness.gui_rpc_port, "getwalletinfo", wallet=wallet_name)
    print(f"[{checkpoints.case_name}] wallet info: {json.dumps(wallet_info, sort_keys=True)}")
    assert wallet_info["external_signer"] is True, "Expected external_signer wallet flag"
    assert wallet_info["private_keys_enabled"] is False, "External signer wallet should disable private keys"
    assert wallet_info["descriptors"] is True, "External signer wallet should be descriptor based"
    assert wallet_info["format"] == "sqlite", "External signer wallet should use sqlite storage"

    receive_address = rpc_call(
        harness.gui_rpc_port,
        "getnewaddress",
        ["", "bech32"],
        wallet=wallet_name,
    )
    print(f"[{checkpoints.case_name}] first bech32 address: {receive_address}")
    assert receive_address == EXPECTED_FIRST_BECH32_ADDRESS, (
        "Expected upstream mock signer to provide the same first bech32 address "
        "used in bitcoin/test/functional/wallet_signer.py"
    )

    address_info = rpc_call(harness.gui_rpc_port, "getaddressinfo", [receive_address], wallet=wallet_name)
    print(f"[{checkpoints.case_name}] address info: {json.dumps(address_info, sort_keys=True)}")
    assert address_info["solvable"] is True, "Expected signer-derived address to be solvable"
    assert address_info["ismine"] is True, "Expected signer-derived address to be recognized as mine"
    assert address_info["hdkeypath"] == EXPECTED_FIRST_HD_KEYPATH, (
        f"Expected first signer-derived address hdkeypath {EXPECTED_FIRST_HD_KEYPATH}, "
        f"got {address_info['hdkeypath']}"
    )
    checkpoints.checkpoint("external wallet rpc descriptors verified", gui)
    return wallet_name


def setup_mock_wallet(gui_rpc_port):
    create_wallet(gui_rpc_port, "mock", blank=True, load_on_startup=False)
    descriptors = [
        {
            "desc": "tr([00000001/86h/1h/0']tprv8ZgxMBicQKsPd7Uf69XL1XwhmjHopUGep8GuEiJDZmbQz6o58LninorQAfcKZWARbtRtfnLcJ5MQ2AtHcQJCCRUcMRvmDUjyEmNUWwx8UbK/0/*)#7ew68cn8",
            "timestamp": 0,
            "range": [0, 1],
            "internal": False,
            "active": True,
        },
        {
            "desc": "tr([00000001/86h/1h/0']tprv8ZgxMBicQKsPd7Uf69XL1XwhmjHopUGep8GuEiJDZmbQz6o58LninorQAfcKZWARbtRtfnLcJ5MQ2AtHcQJCCRUcMRvmDUjyEmNUWwx8UbK/1/*)#0dtm6drl",
            "timestamp": 0,
            "range": [0, 0],
            "internal": True,
            "active": True,
        },
    ]
    result = rpc_call(gui_rpc_port, "importdescriptors", [descriptors], wallet="mock")
    assert result[0]["success"] is True
    assert result[1]["success"] is True


def prepare_signed_mock_psbt(harness, external_wallet_name, destination_address):
    receive_address = rpc_call(
        harness.gui_rpc_port,
        "getnewaddress",
        ["", "bech32m"],
        wallet=external_wallet_name,
    )
    print(f"[qml_external_signer] first bech32m address: {receive_address}")
    assert receive_address == EXPECTED_FIRST_BECH32M_ADDRESS, (
        "Expected upstream mock signer to provide the same first bech32m address "
        "used in bitcoin/test/functional/wallet_signer.py"
    )
    address_info = rpc_call(harness.gui_rpc_port, "getaddressinfo", [receive_address], wallet=external_wallet_name)
    assert address_info["hdkeypath"] == EXPECTED_FIRST_TAPROOT_HD_KEYPATH, (
        f"Expected first taproot signer-derived address hdkeypath {EXPECTED_FIRST_TAPROOT_HD_KEYPATH}, "
        f"got {address_info['hdkeypath']}"
    )

    rpc_call(harness.gui_rpc_port, "generatetoaddress", [101, receive_address])

    mock_psbt = rpc_call(
        harness.gui_rpc_port,
        "walletcreatefundedpsbt",
        [[], {destination_address: 0.5}, 0, {"replaceable": True, "feeRate": 0.00001000}, True],
        wallet="mock",
    )["psbt"]
    processed = rpc_call(
        harness.gui_rpc_port,
        "walletprocesspsbt",
        [mock_psbt, True, "ALL", True, True],
        wallet="mock",
    )
    mock_tx = processed["hex"]
    assert rpc_call(harness.gui_rpc_port, "testmempoolaccept", [[mock_tx]])[0]["allowed"] is True

    mock_psbt_path = os.path.join(harness.tmpdir, "mock_psbt")
    with open(mock_psbt_path, "w", encoding="utf8") as mock_psbt_file:
        mock_psbt_file.write(processed["psbt"])

    return rpc_call(harness.gui_rpc_port, "decoderawtransaction", [mock_tx])["txid"]


def set_multiple_recipients(gui, enabled):
    gui.click("sendOptionsButton")
    gui.wait_for_property("sendOptionsPopup", "opened", True, timeout_ms=5000)
    current = gui.get_property("sendOptionsMultipleRecipientsToggle", "checked")
    if bool(current) != enabled:
        gui.click("sendOptionsMultipleRecipientsToggle")
        gui.wait_for_property("sendOptionsMultipleRecipientsToggle", "checked", enabled, timeout_ms=5000)
    gui.click("sendOptionsButton")
    gui.wait_for_property("sendOptionsPopup", "opened", False, timeout_ms=5000)


def set_amount_unit(gui, unit_label):
    current = gui.get_property("sendAmountUnitLabel", "text")
    if current != unit_label:
        gui.click("sendAmountUnitToggle")
        gui.wait_for_property("sendAmountUnitLabel", "text", unit_label, timeout_ms=5000)


def open_external_signer_review(gui, destination_address, amount_text):
    gui.click("sendTabButton")
    gui.wait_for_page("sendPage", timeout_ms=10000)
    set_multiple_recipients(gui, False)
    set_amount_unit(gui, "₿")
    gui.set_text("sendAddressInput", destination_address)
    gui.set_text("sendAmountInput", amount_text)
    gui.wait_for_property("sendReviewButton", "enabled", True, timeout_ms=10000)
    gui.click("sendReviewButton")
    gui.wait_for_page("sendReviewPage", timeout_ms=10000)


def assert_signer_status(gui, object_name, expected_text):
    wait_until(
        lambda: gui.get_text(object_name) == expected_text,
        description=f"{object_name} text",
    )


def fail_case(case_name, harness, checkpoints, err):
    print(f"\nFAILED [{case_name}]: {err}", file=sys.stderr)
    import traceback
    traceback.print_exc()
    if harness.driver is not None:
        try:
            checkpoints.checkpoint("failure state", harness.driver)
        except Exception as screenshot_err:  # noqa: BLE001 - preserve original failure context
            print(f"[{case_name}] failed to save failure screenshot: {screenshot_err}", file=sys.stderr)
    gui_output = harness.process_output(harness.gui_process)
    if gui_output:
        print("\n--- GUI process output ---", file=sys.stderr)
        print(gui_output, file=sys.stderr)
    if harness.driver is not None:
        dump_qml_tree(harness.driver)


def run_test(args):
    case_name = "qml_external_signer"
    screenshot_root = None
    if args.save_screenshots:
        screenshot_root = make_screenshot_root()
        print(f"Checkpoint screenshots will be saved under: {screenshot_root}")

    harness = WalletFlowHarness(case_name, port_offset=90)
    checkpoints = CheckpointRecorder(case_name, args.save_screenshots, screenshot_root)
    try:
        signer_path = find_mock_signer_path("signer")
        no_signer_path = find_mock_signer_path("no_signer")

        print(f"[{case_name}] starting")
        harness.start_gui(cwd=harness.tmpdir)
        checkpoints.checkpoint("GUI launched", harness.driver)
        harness.finish_onboarding()
        wait_for_rpc(harness.gui_rpc_port)
        checkpoints.checkpoint("onboarding completed", harness.driver)

        expect_runtime_error(
            lambda: rpc_call(harness.gui_rpc_port, "enumeratesigners"),
            "restart bitcoind with -signer=<cmd>",
        )
        expect_runtime_error(
            lambda: create_wallet(harness.gui_rpc_port, "missing_signer", disable_private_keys=True, external_signer=True),
            "restart bitcoind with -signer=<cmd>",
        )
        checkpoints.checkpoint("missing signer setting rejected", harness.driver)

        configure_external_signer_via_gui(harness, checkpoints, signer_path)
        wallet_name = create_and_verify_external_wallet(harness, checkpoints)
        open_selected_wallet_settings(harness.driver)
        harness.driver.wait_for_property("walletPasswordRow", "visible", False, timeout_ms=10000)
        harness.driver.wait_for_property("walletBackupRow", "visible", True, timeout_ms=10000)
        checkpoints.checkpoint("external signer wallet hides password settings", harness.driver)

        create_wallet(harness.gui_rpc_port, "miner", load_on_startup=False)
        setup_mock_wallet(harness.gui_rpc_port)

        destination_address = rpc_call(harness.gui_rpc_port, "getnewaddress", wallet="miner")
        expected_txid = prepare_signed_mock_psbt(harness, wallet_name, destination_address)
        checkpoints.checkpoint("signed mock PSBT prepared", harness.driver)
        select_wallet(harness.driver, wallet_name)
        checkpoints.checkpoint("external signer wallet reselected", harness.driver)
        wait_for_wallet_badge_balance(harness.driver, "50.00000000")
        checkpoints.checkpoint("wallet badge balance updated", harness.driver)

        harness.restart_gui(cwd=harness.tmpdir)
        wait_for_rpc(harness.gui_rpc_port)
        signers = rpc_call(harness.gui_rpc_port, "enumeratesigners")["signers"]
        assert {"fingerprint": "00000001", "name": "trezor_t"} in signers
        wait_for_wallet(harness.driver, wallet_name)
        wait_for_wallet_balance(harness.gui_rpc_port, wallet_name)
        wait_for_wallet_badge_balance(harness.driver, "50.00000000")
        checkpoints.checkpoint("wallet badge balance restored after restart", harness.driver)

        harness.update_gui_settings({"signer": no_signer_path})
        harness.restart_gui(cwd=harness.tmpdir)
        wait_for_rpc(harness.gui_rpc_port)
        assert rpc_call(harness.gui_rpc_port, "enumeratesigners")["signers"] == []
        wait_for_wallet(harness.driver, wallet_name)
        wait_for_wallet_balance(harness.gui_rpc_port, wallet_name)
        wait_for_wallet_badge_balance(harness.driver, "50.00000000")
        checkpoints.checkpoint("wallet badge balance confirmed without signer", harness.driver)

        open_external_signer_review(harness.driver, destination_address, "0.50000000")
        checkpoints.checkpoint("no-signer review displayed", harness.driver)
        assert_signer_status(
            harness.driver,
            "sendReviewStatusText",
            "Approve on external signer to broadcast this transaction.",
        )
        harness.driver.click("sendReviewExternalSignerButton")
        assert_signer_status(
            harness.driver,
            "sendReviewStatusText",
            "External signer not found. Connect one device and try again.",
        )
        checkpoints.checkpoint("no-signer review error surfaced", harness.driver)

        harness.update_gui_settings({"signer": signer_path})
        harness.restart_gui(cwd=harness.tmpdir)
        wait_for_rpc(harness.gui_rpc_port)
        signers = rpc_call(harness.gui_rpc_port, "enumeratesigners")["signers"]
        assert {"fingerprint": "00000001", "name": "trezor_t"} in signers
        wait_for_wallet(harness.driver, wallet_name)
        wait_for_wallet_balance(harness.gui_rpc_port, wallet_name)
        wait_for_wallet_badge_balance(harness.driver, "50.00000000")
        checkpoints.checkpoint("wallet badge balance confirmed after signer restore", harness.driver)

        open_external_signer_review(harness.driver, destination_address, "0.50000000")
        checkpoints.checkpoint("signer review displayed", harness.driver)
        harness.driver.click("sendReviewExternalSignerButton")
        assert_signer_status(
            harness.driver,
            "sendReviewStatusText",
            "Signed on external signer. Ready to send.",
        )
        checkpoints.checkpoint("signer approval completed", harness.driver)
        harness.driver.click("sendReviewExternalSignerButton")
        wait_until(
            lambda: expected_txid in rpc_call(harness.gui_rpc_port, "getrawmempool"),
            description="signed transaction broadcast",
        )
        checkpoints.checkpoint("signed transaction broadcast", harness.driver)

        print(f"[{case_name}] completed")
        print("External signer GUI flow passed.")
        return 0
    except Exception as err:  # noqa: BLE001 - preserve failure context for functional test output
        fail_case(case_name, harness, checkpoints, err)
        return 1
    finally:
        harness.stop()


if __name__ == "__main__":
    sys.exit(run_test(parse_args()))
