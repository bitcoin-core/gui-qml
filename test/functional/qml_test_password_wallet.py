#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""End-to-end GUI tests for password wallet flows."""

import argparse
import os
import re
import signal
import subprocess
import sys
import time
from datetime import datetime

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "bitcoin", "test", "functional"))

from qml_driver import QmlDriverError
from qml_test_harness import dump_qml_tree
from qml_wallet_test_lib import (
    WalletFlowHarness,
    find_legacy_bitcoind,
    rpc_call,
    wait_for_rpc,
)
from test_framework.descriptors import descsum_create


WALLET_PASSWORD = "correct horse battery staple"
FALLBACK_DESC_EXTERNAL = "wpkh(tprv8ZgxMBicQKsPdYeeZbPSKd2KYLmeVKtcFA7kqCxDvDR13MQ6us8HopUR2wLcS2ZKPhLyKsqpDL2FtL73LMHcgoCL7DXsciA8eX8nbjCR2eG/0h/*h)"
FALLBACK_DESC_INTERNAL = "wpkh(tprv8ZgxMBicQKsPdYeeZbPSKd2KYLmeVKtcFA7kqCxDvDR13MQ6us8HopUR2wLcS2ZKPhLyKsqpDL2FtL73LMHcgoCL7DXsciA8eX8nbjCR2eG/1h/*h)"


def parse_args():
    parser = argparse.ArgumentParser(
        description="Password wallet GUI functional test",
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
    screenshot_root = os.path.join(artifacts_root, f"qml_test_password_wallet-{timestamp}")
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


def sanitize_object_suffix(value):
    return re.sub(r"[^A-Za-z0-9_]+", "_", value)


def start_node(binary, datadir, rpc_port, extra_args=None):
    args = [binary, f"-datadir={datadir}"]
    if extra_args:
        args.extend(extra_args)
    process = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    wait_for_rpc(rpc_port)
    return process


def stop_node(process, rpc_port=None):
    if process and process.poll() is None:
        if rpc_port is not None:
            try:
                rpc_call(rpc_port, "stop")
            except Exception:
                process.send_signal(signal.SIGTERM)
        else:
            process.send_signal(signal.SIGTERM)
        try:
            process.wait(timeout=20)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


def create_recipient_wallet(harness, wallet_name="recipient"):
    harness.start_source_node()
    rpc_call(harness.source_rpc_port, "createwallet", {"wallet_name": wallet_name})
    return rpc_call(harness.source_rpc_port, "getnewaddress", wallet=wallet_name)


def create_password_wallet(gui, wallet_name, password):
    gui.wait_for_property("createWalletButton", "visible", True, timeout_ms=10000)
    gui.wait_for_property("createWalletButton", "enabled", True, timeout_ms=25000)
    gui.click("createWalletButton")
    gui.click("createWalletIntroStartButton")
    gui.set_text("createWalletNameInput", wallet_name)
    gui.click("createWalletNameContinueButton")
    gui.set_text("createWalletPasswordInput", password)
    gui.set_text("createWalletPasswordRepeatInput", password)
    gui.click("createWalletPasswordConfirmToggle")
    gui.wait_for_property("createWalletPasswordContinueButton", "enabled", True, timeout_ms=25000)
    gui.click("createWalletPasswordContinueButton")
    gui.wait_for_property("createWalletConfirmNextButton", "visible", True, timeout_ms=10000)
    gui.click("createWalletConfirmNextButton")
    gui.wait_for_property("createWalletBackupDoneButton", "visible", True, timeout_ms=10000)
    gui.click("createWalletBackupDoneButton")


def dismiss_create_wallet_wizard(gui):
    gui.wait_for_property("createWalletWizardExitButton", "visible", True, timeout_ms=10000)
    gui.click("createWalletWizardExitButton")


def wait_for_wallet_ready(harness, gui):
    wait_for_rpc(harness.gui_rpc_port, timeout=60)
    gui.wait_for_property("walletBadge", "loading", False, timeout_ms=25000)


def mine_to_wallet(gui_rpc_port, wallet_name, blocks):
    address = rpc_call(gui_rpc_port, "getnewaddress", wallet=wallet_name)
    rpc_call(gui_rpc_port, "generatetoaddress", [blocks, address])


def open_send_tab(gui):
    gui.click("walletSendTab")
    gui.wait_for_property("walletSendTitle", "visible", True, timeout_ms=5000)


def fill_send_form(gui, address, amount):
    gui.set_text("sendAddressInput", address)
    gui.set_text("sendAmountInput", amount)


def assert_wallet_locked(gui_rpc_port, wallet_name):
    info = rpc_call(gui_rpc_port, "getwalletinfo", wallet=wallet_name)
    assert info["unlocked_until"] == 0, f"Expected locked wallet, got getwalletinfo={info}"


def open_wallet_selector(gui):
    gui.wait_for_property("walletBadge", "loading", False, timeout_ms=20000)
    gui.click("walletBadge")
    gui.wait_for_property("walletSelectPopup", "opened", True, timeout_ms=5000)


def select_wallet(gui, wallet_name):
    open_wallet_selector(gui)
    wallet_selector_object_name = f"walletSelectItem_{sanitize_object_suffix(wallet_name)}"
    gui.wait_for_object(wallet_selector_object_name, timeout_ms=5000)
    gui.click(wallet_selector_object_name)


def open_import_wallet_page(gui):
    gui.wait_for_property("importWalletButton", "visible", True, timeout_ms=10000)
    gui.wait_for_property("importWalletButton", "enabled", True, timeout_ms=25000)
    gui.click("importWalletButton")
    gui.wait_for_page("importWalletOptions", timeout_ms=10000)


def trigger_automated_import(gui, backup_path):
    gui.set_text("importWalletPathField", backup_path)
    gui.click("importWalletChooseFileButton")


def drain_change_keypool(gui_rpc_port, wallet_name):
    while True:
        try:
            rpc_call(gui_rpc_port, "getrawchangeaddress", wallet=wallet_name)
        except RuntimeError as err:
            assert "Keypool ran out" in str(err), f"Unexpected keypool drain failure: {err}"
            return


def configure_fallback_wallet(harness, wallet_name):
    process = start_node(harness.bitcoind_binary, harness.gui_datadir, harness.gui_rpc_port)
    try:
        rpc_call(harness.gui_rpc_port, "createwallet", {"wallet_name": wallet_name, "blank": True})
        rpc_call(harness.gui_rpc_port, "encryptwallet", [WALLET_PASSWORD], wallet=wallet_name)
        rpc_call(harness.gui_rpc_port, "walletpassphrase", [WALLET_PASSWORD, 60], wallet=wallet_name)
        external_desc = descsum_create(FALLBACK_DESC_EXTERNAL)
        internal_desc = descsum_create(FALLBACK_DESC_INTERNAL)
        rpc_call(
            harness.gui_rpc_port,
            "importdescriptors",
            [[
                {
                    "desc": external_desc,
                    "timestamp": "now",
                    "active": True,
                    "range": [0, 0],
                },
                {
                    "desc": internal_desc,
                    "timestamp": "now",
                    "active": True,
                    "internal": True,
                    "range": [0, 0],
                },
            ]],
            wallet=wallet_name,
        )
        rpc_call(harness.gui_rpc_port, "keypoolrefill", [1], wallet=wallet_name)
        rpc_call(harness.gui_rpc_port, "walletlock", wallet=wallet_name)
        mine_to_wallet(harness.gui_rpc_port, wallet_name, 101)
        drain_change_keypool(harness.gui_rpc_port, wallet_name)
    finally:
        stop_node(process, harness.gui_rpc_port)


def configure_managed_legacy_wallet(harness, wallet_name):
    legacy_binary = find_legacy_bitcoind()
    if not legacy_binary:
        return None

    process = start_node(
        legacy_binary,
        harness.gui_datadir,
        harness.gui_rpc_port,
        extra_args=["-deprecatedrpc=create_bdb"],
    )
    try:
        rpc_call(
            harness.gui_rpc_port,
            "createwallet",
            {
                "wallet_name": wallet_name,
                "descriptors": False,
            },
        )
        rpc_call(harness.gui_rpc_port, "encryptwallet", [WALLET_PASSWORD], wallet=wallet_name)
    finally:
        stop_node(process, harness.gui_rpc_port)
    return legacy_binary


def run_case(case_name, port_offset, case_body, save_screenshots=False, screenshot_root=None):
    harness = WalletFlowHarness(case_name, port_offset=port_offset)
    checkpoints = CheckpointRecorder(case_name, save_screenshots, screenshot_root)
    try:
        print(f"[{case_name}] starting")
        case_body(harness, checkpoints)
        print(f"[{case_name}] completed")
        return 0
    except Exception as err:  # noqa: BLE001 - preserve failure context for functional test output
        print(f"\nFAILED [{case_name}]: {err}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        gui = harness.driver
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
        return 1
    finally:
        harness.stop()


def case_created_wallet_send(harness, checkpoints):
    wallet_name = "created_password_wallet"
    recipient_addr = create_recipient_wallet(harness)

    harness.start_gui(reset_gui_settings=True)
    gui = harness.driver
    checkpoints.checkpoint("GUI launched", gui)
    harness.finish_onboarding()
    checkpoints.checkpoint("onboarding completed", gui)

    create_password_wallet(gui, wallet_name, WALLET_PASSWORD)
    gui.wait_for_property("walletBadge", "text", wallet_name, timeout_ms=20000)
    wait_for_wallet_ready(harness, gui)
    checkpoints.checkpoint("encrypted wallet created", gui)

    mine_to_wallet(harness.gui_rpc_port, wallet_name, 101)
    assert_wallet_locked(harness.gui_rpc_port, wallet_name)
    checkpoints.checkpoint("wallet funded and locked", gui)

    harness.stop_gui()
    harness.start_gui()
    gui = harness.driver
    gui.wait_for_property("walletBadge", "text", wallet_name, timeout_ms=20000)
    assert_wallet_locked(harness.gui_rpc_port, wallet_name)
    checkpoints.checkpoint("wallet restarted locked", gui)

    gui.click("walletActivityTab")
    gui.wait_for_property("walletActivityTitle", "visible", True, timeout_ms=5000)
    checkpoints.checkpoint("locked wallet activity visible", gui)

    open_send_tab(gui)
    fill_send_form(gui, recipient_addr, "1")
    gui.click("sendReviewButton")
    gui.wait_for_property("reviewPassphrasePopup", "opened", True, timeout_ms=10000)
    checkpoints.checkpoint("review passphrase prompt displayed", gui)
    gui.set_text("reviewPassphraseField", WALLET_PASSWORD)
    gui.click("reviewPassphraseConfirmButton")
    gui.wait_for_property("sendTransactionButton", "visible", True, timeout_ms=20000)
    assert_wallet_locked(harness.gui_rpc_port, wallet_name)
    checkpoints.checkpoint("review built and wallet relocked", gui)

    before = rpc_call(harness.gui_rpc_port, "getwalletinfo", wallet=wallet_name)["txcount"]
    gui.click("sendTransactionButton")
    gui.wait_for_property("sendResultPopup", "opened", True, timeout_ms=20000)
    checkpoints.checkpoint("prepared transaction broadcast", gui)

    after = rpc_call(harness.gui_rpc_port, "getwalletinfo", wallet=wallet_name)["txcount"]
    assert after > before, f"Expected additional wallet transaction, before={before}, after={after}"
    assert_wallet_locked(harness.gui_rpc_port, wallet_name)


def case_locked_review_fallback(harness, checkpoints):
    wallet_name = "fallback_password_wallet"
    recipient_addr = create_recipient_wallet(harness, wallet_name="fallback_recipient")
    configure_fallback_wallet(harness, wallet_name)
    checkpoints.checkpoint("fallback wallet fixture prepared")

    harness.start_gui(reset_gui_settings=True)
    gui = harness.driver
    checkpoints.checkpoint("GUI launched", gui)
    harness.finish_onboarding()
    dismiss_create_wallet_wizard(gui)
    wait_for_wallet_ready(harness, gui)
    rpc_call(harness.gui_rpc_port, "loadwallet", [wallet_name])
    gui.wait_for_property("walletBadge", "text", wallet_name, timeout_ms=20000)
    assert_wallet_locked(harness.gui_rpc_port, wallet_name)
    checkpoints.checkpoint("fallback wallet selected", gui)

    open_send_tab(gui)
    fill_send_form(gui, recipient_addr, "1")
    gui.click("sendReviewButton")
    gui.wait_for_property("reviewPassphrasePopup", "opened", True, timeout_ms=10000)
    checkpoints.checkpoint("review fallback passphrase prompt displayed", gui)

    gui.set_text("reviewPassphraseField", WALLET_PASSWORD)
    gui.click("reviewPassphraseConfirmButton")
    gui.wait_for_property("sendTransactionButton", "visible", True, timeout_ms=20000)
    assert_wallet_locked(harness.gui_rpc_port, wallet_name)
    checkpoints.checkpoint("review rebuilt and wallet relocked", gui)

    gui.click("sendTransactionButton")
    gui.wait_for_property("sendResultPopup", "opened", True, timeout_ms=20000)
    checkpoints.checkpoint("fallback transaction broadcast", gui)
    assert_wallet_locked(harness.gui_rpc_port, wallet_name)


def case_import_encrypted_wallet(harness, checkpoints):
    wallet_name = "imported_password_wallet"
    backup_path = os.path.join(harness.tmpdir, f"{wallet_name}.bak")
    harness.start_source_node()
    rpc_call(harness.source_rpc_port, "createwallet", {"wallet_name": wallet_name, "passphrase": WALLET_PASSWORD})
    source_address = rpc_call(harness.source_rpc_port, "getnewaddress", wallet=wallet_name)
    rpc_call(harness.source_rpc_port, "generatetoaddress", [101, source_address])
    rpc_call(harness.source_rpc_port, "backupwallet", [backup_path], wallet=wallet_name)
    harness.stop_source_node()
    checkpoints.checkpoint("encrypted backup fixture created")

    harness.start_gui(reset_gui_settings=True)
    gui = harness.driver
    checkpoints.checkpoint("GUI launched", gui)
    harness.finish_onboarding()
    open_import_wallet_page(gui)
    checkpoints.checkpoint("import flow opened", gui)

    trigger_automated_import(gui, backup_path)
    gui.wait_for_page("importWalletSuccessPage", timeout_ms=20000)
    gui.click("importWalletSuccessOverviewButton")
    gui.wait_for_property("walletBadge", "text", wallet_name, timeout_ms=20000)
    wait_for_wallet_ready(harness, gui)
    assert_wallet_locked(harness.gui_rpc_port, wallet_name)
    checkpoints.checkpoint("encrypted wallet imported locked", gui)

    gui.click("walletActivityTab")
    gui.wait_for_property("walletActivityTitle", "visible", True, timeout_ms=5000)
    checkpoints.checkpoint("imported locked wallet activity visible", gui)


def case_managed_legacy_migration(harness, checkpoints):
    wallet_name = "legacy_encrypted_wallet"
    legacy_binary = configure_managed_legacy_wallet(harness, wallet_name)
    if not legacy_binary:
        print("SKIPPED [qml_password_wallet_managed_legacy_migration]: legacy bitcoind not found.")
        print("Set BITCOIND_LEGACY or provide releases/v28.0/bin/bitcoind to exercise this flow.")
        return
    checkpoints.checkpoint("managed legacy wallet fixture prepared")

    harness.start_gui(reset_gui_settings=True)
    gui = harness.driver
    checkpoints.checkpoint("GUI launched", gui)
    harness.finish_onboarding()
    dismiss_create_wallet_wizard(gui)
    wait_for_wallet_ready(harness, gui)
    checkpoints.checkpoint("wallet overview displayed", gui)

    select_wallet(gui, wallet_name)
    gui.wait_for_property("walletMigrationPopup", "opened", True, timeout_ms=10000)
    checkpoints.checkpoint("legacy migration prompt displayed", gui)

    gui.click("walletMigrationConfirmButton")
    gui.wait_for_property("walletMigrationPassphrasePopup", "opened", True, timeout_ms=10000)
    assert gui.get_text("walletMigrationPassphraseErrorText") == ""
    checkpoints.checkpoint("migration passphrase prompt displayed", gui)

    gui.set_text("walletMigrationPassphraseField", "wrong password")
    gui.click("walletMigrationPassphraseConfirmButton")
    gui.wait_for_property(
        "walletMigrationPassphraseErrorText",
        "text",
        lambda text: "passphrase" in text.lower(),
        timeout_ms=10000,
    )
    checkpoints.checkpoint("wrong migration password rejected", gui)

    gui.set_text("walletMigrationPassphraseField", WALLET_PASSWORD)
    gui.click("walletMigrationPassphraseConfirmButton")
    gui.wait_for_property("walletBadge", "text", wallet_name, timeout_ms=20000)
    assert_wallet_locked(harness.gui_rpc_port, wallet_name)
    checkpoints.checkpoint("legacy wallet migrated and loaded", gui)


def run_test(args):
    screenshot_root = None
    if args.save_screenshots:
        screenshot_root = make_screenshot_root()
        print(f"Checkpoint screenshots will be saved under: {screenshot_root}")

    cases = [
        ("qml_password_wallet_created_send", 300, case_created_wallet_send),
        ("qml_password_wallet_review_fallback", 310, case_locked_review_fallback),
        ("qml_password_wallet_import_encrypted", 320, case_import_encrypted_wallet),
        ("qml_password_wallet_managed_legacy_migration", 330, case_managed_legacy_migration),
    ]

    exit_code = 0
    for case_name, port_offset, case_body in cases:
        exit_code |= run_case(
            case_name,
            port_offset,
            case_body,
            save_screenshots=args.save_screenshots,
            screenshot_root=screenshot_root,
        )
    return exit_code


if __name__ == "__main__":
    sys.exit(run_test(parse_args()))
