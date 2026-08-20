#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""End-to-end GUI tests for wallet settings flows."""

import argparse
import os
import re
import signal
import subprocess
import sys
import time
from datetime import datetime

from qml_driver import QmlDriverError
from qml_test_harness import dump_qml_tree
from qml_wallet_test_lib import WalletFlowHarness, find_bitcoind, rpc_call, wait_for_rpc


WALLET_PASSWORD = "correct horse battery staple"


def parse_args():
    parser = argparse.ArgumentParser(
        description="Wallet settings GUI functional test",
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
    screenshot_root = os.path.join(artifacts_root, f"qml_test_wallet_settings-{timestamp}")
    os.makedirs(screenshot_root, exist_ok=True)
    return screenshot_root


class CheckpointRecorder:
    STACK_VIEW_NAMES = ("mainPageStack", "createWalletWizard", "settingsNavigationStack_wallet")

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

        gui.settle(stack_view_names=self.STACK_VIEW_NAMES)

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


def prepare_managed_wallet(harness, wallet_name, password):
    harness.bitcoind_binary = harness.bitcoind_binary or find_bitcoind()
    process = start_node(harness.bitcoind_binary, harness.gui_datadir, harness.gui_rpc_port)
    try:
        rpc_call(
            harness.gui_rpc_port,
            "createwallet",
            {
                "wallet_name": wallet_name,
                "passphrase": password,
            },
        )
    finally:
        stop_node(process, harness.gui_rpc_port)


def dismiss_create_wallet_wizard(gui):
    try:
        gui.wait_for_property("createWalletWizardExitButton", "visible", True, timeout_ms=1000)
        gui.click("createWalletWizardExitButton")
    except QmlDriverError:
        gui.wait_for_property("typeSelectorCancelButton", "visible", True, timeout_ms=10000)
        gui.click("typeSelectorCancelButton")


def wait_for_wallet_ready(harness, gui):
    wait_for_rpc(harness.gui_rpc_port, timeout=60)
    gui.wait_for_property("walletBadge", "loading", False, timeout_ms=25000)


def load_wallet(gui, harness, wallet_name):
    wait_for_rpc(harness.gui_rpc_port, timeout=60)
    rpc_call(harness.gui_rpc_port, "loadwallet", [wallet_name])
    wait_for_wallet_ready(harness, gui)
    gui.wait_for_property("walletBadge", "noWalletLoaded", False, timeout_ms=5000)


def open_wallet_settings(gui):
    gui.click("desktopWalletSettingsTabButton")
    gui.settle()
    gui.wait_for_property("settingsSidebar_wallet", "visible", True, timeout_ms=5000)
    gui.click("settingsSidebar_wallet")
    gui.wait_for_page("walletSettingsPage", timeout_ms=10000)


def open_wallet_selector(gui):
    gui.wait_for_property("walletBadge", "loading", False, timeout_ms=20000)
    if gui.get_property("walletSelectPopup", "opened") is True:
        return
    gui.click("walletBadge")
    gui.wait_for_property("walletSelectPopup", "opened", True, timeout_ms=5000)


def select_wallet(gui, wallet_name):
    open_wallet_selector(gui)
    item_name = f"walletSelectItem_{sanitize_object_suffix(wallet_name)}"
    gui.wait_for_object(item_name, timeout_ms=5000)
    gui.click(item_name)


def close_wallet_from_selector(gui, wallet_name):
    if gui.get_property("walletSelectPopup", "opened") is not True:
        open_wallet_selector(gui)
    gui.settle()
    deadline = time.time() + 5
    object_name = f"walletSelectClose_{sanitize_object_suffix(wallet_name)}"
    while True:
        try:
            if gui.get_property(object_name, "visible") is True:
                break
        except QmlDriverError:
            pass
        if time.time() >= deadline:
            raise AssertionError(f"Close button did not appear for wallet {wallet_name!r}")
        time.sleep(0.05)
    gui.click(object_name)


def wait_for_file(path, timeout=20):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if os.path.isfile(path) and os.path.getsize(path) > 0:
            return
        time.sleep(0.1)
    raise AssertionError(f"Timed out waiting for file at {path}")


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


def case_rename_persists_across_restart(harness, checkpoints):
    wallet_name = "settings_rename_wallet"
    display_name = "Daily spending"

    prepare_managed_wallet(harness, wallet_name, WALLET_PASSWORD)
    checkpoints.checkpoint("managed wallet fixture prepared")

    harness.start_gui()
    gui = harness.driver
    checkpoints.checkpoint("GUI launched", gui)
    harness.finish_onboarding()
    checkpoints.checkpoint("onboarding completed", gui)
    dismiss_create_wallet_wizard(gui)
    load_wallet(gui, harness, wallet_name)
    checkpoints.checkpoint("managed wallet loaded", gui)

    open_wallet_settings(gui)
    checkpoints.checkpoint("wallet settings opened", gui)

    gui.wait_for_property("walletNameInput", "visible", True, timeout_ms=5000)
    gui.set_text("walletNameInput", display_name)
    gui.invoke("walletNameInput", "editingFinished")
    gui.wait_for_property("walletBadge", "text", display_name, timeout_ms=5000)
    checkpoints.checkpoint("wallet renamed", gui)

    harness.stop_gui()
    harness.start_gui()
    gui = harness.driver
    load_wallet(gui, harness, wallet_name)
    gui.wait_for_property("walletBadge", "text", display_name, timeout_ms=20000)
    checkpoints.checkpoint("display name persisted after restart", gui)


def case_backup_uses_automation_path(harness, checkpoints):
    wallet_name = "settings_backup_wallet"
    backup_dir = os.path.join(harness.tmpdir, "wallet_backups")
    os.makedirs(backup_dir, exist_ok=True)
    backup_path = os.path.join(backup_dir, f"{wallet_name}.bak")

    prepare_managed_wallet(harness, wallet_name, WALLET_PASSWORD)
    checkpoints.checkpoint("managed wallet fixture prepared")

    harness.start_gui()
    gui = harness.driver
    checkpoints.checkpoint("GUI launched", gui)
    harness.finish_onboarding()
    checkpoints.checkpoint("onboarding completed", gui)
    dismiss_create_wallet_wizard(gui)
    load_wallet(gui, harness, wallet_name)
    checkpoints.checkpoint("managed wallet loaded", gui)

    open_wallet_settings(gui)
    checkpoints.checkpoint("wallet settings opened", gui)

    gui.set_text("walletSettingsBackupPathField", backup_dir)
    gui.click("walletBackupRow")
    wait_for_file(backup_path)
    assert gui.get_property("walletSettingsPage", "errorText") == "", (
        "Backup should not surface an error"
    )
    checkpoints.checkpoint("wallet backup created", gui)


def case_sign_verify_message(harness, checkpoints):
    wallet_name = "settings_sign_verify_wallet"
    message = "I control this address."

    prepare_managed_wallet(harness, wallet_name, WALLET_PASSWORD)
    checkpoints.checkpoint("managed wallet fixture prepared")

    harness.start_gui()
    gui = harness.driver
    checkpoints.checkpoint("GUI launched", gui)
    harness.finish_onboarding()
    checkpoints.checkpoint("onboarding completed", gui)
    dismiss_create_wallet_wizard(gui)
    load_wallet(gui, harness, wallet_name)
    checkpoints.checkpoint("managed wallet loaded", gui)

    address = rpc_call(harness.gui_rpc_port, "getnewaddress", ["", "legacy"], wallet=wallet_name)

    open_wallet_settings(gui)
    gui.click("walletSignVerifyMessageRow")
    gui.wait_for_page("signVerifyMessagePage", timeout_ms=10000)
    checkpoints.checkpoint("sign verify message page opened", gui)

    gui.set_text("signMessageAddressField", address)
    gui.set_text("signMessageMessageField", message)
    gui.wait_for_property("signMessageButton", "enabled", True, timeout_ms=5000)
    gui.click("signMessageButton")
    gui.wait_for_property("signMessagePassphrasePopup", "opened", True, timeout_ms=5000)
    gui.set_text("signMessagePassphraseField", WALLET_PASSWORD)
    gui.click("signMessagePassphraseConfirmButton")
    gui.wait_for_property("signMessageSignatureOutput", "visible", True, timeout_ms=10000)
    signature = gui.get_text("signMessageSignatureText")
    assert signature, "Signing should reveal a signature"
    checkpoints.checkpoint("message signed", gui)

    gui.click("verifyMessageTab")
    gui.set_text("verifyMessageAddressField", address)
    gui.set_text("verifyMessageMessageField", message)
    gui.set_text("verifyMessageSignatureField", signature)
    gui.wait_for_property("verifyMessageButton", "enabled", True, timeout_ms=5000)
    gui.click("verifyMessageButton")
    gui.wait_for_property("verifyMessageResultBanner", "visible", True, timeout_ms=5000)
    assert gui.get_text("verifyMessageResultText") == "Message verified successfully."
    checkpoints.checkpoint("message verified", gui)

    gui.set_text("verifyMessageMessageField", message + " changed")
    gui.click("verifyMessageButton")
    gui.wait_for_property("verifyMessageResultBanner", "visible", True, timeout_ms=5000)
    assert gui.get_text("verifyMessageResultText") == "Message verification failed."
    checkpoints.checkpoint("message verification failure displayed", gui)


def case_subpages_close_when_wallet_becomes_unselected(harness, checkpoints):
    wallet_name = "settings_subpage_wallet"

    prepare_managed_wallet(harness, wallet_name, WALLET_PASSWORD)
    checkpoints.checkpoint("managed wallet fixture prepared")

    harness.start_gui()
    gui = harness.driver
    checkpoints.checkpoint("GUI launched", gui)
    harness.finish_onboarding()
    checkpoints.checkpoint("onboarding completed", gui)
    dismiss_create_wallet_wizard(gui)
    load_wallet(gui, harness, wallet_name)
    checkpoints.checkpoint("managed wallet loaded", gui)

    open_wallet_settings(gui)
    gui.click("walletPasswordRow")
    gui.wait_for_page("walletPasswordSettingsPage", timeout_ms=10000)
    checkpoints.checkpoint("password subpage opened", gui)

    close_wallet_from_selector(gui, wallet_name)
    gui.wait_for_property("walletCloseConfirmationPopup", "opened", True, timeout_ms=5000)
    gui.click("walletCloseConfirmationConfirmButton")
    gui.wait_for_property("walletCloseConfirmationPopup", "opened", False, timeout_ms=5000)
    gui.wait_for_page("walletSettingsPage", timeout_ms=10000)
    gui.wait_for_property("walletBadge", "noWalletLoaded", True, timeout_ms=5000)
    checkpoints.checkpoint("password subpage unwound after wallet close", gui)

    select_wallet(gui, wallet_name)
    wait_for_wallet_ready(harness, gui)
    gui.wait_for_property("walletBadge", "noWalletLoaded", False, timeout_ms=5000)
    gui.wait_for_property("walletNameInput", "visible", True, timeout_ms=5000)
    checkpoints.checkpoint("wallet reselected from settings page", gui)


def case_password_page_closes_when_selected_wallet_changes(harness, checkpoints):
    wallet_names = ["settings_password_alpha_wallet", "settings_password_beta_wallet"]

    for wallet_name in wallet_names:
        prepare_managed_wallet(harness, wallet_name, WALLET_PASSWORD)
    checkpoints.checkpoint("two managed wallets prepared")

    harness.start_gui()
    gui = harness.driver
    checkpoints.checkpoint("GUI launched", gui)
    harness.finish_onboarding()
    checkpoints.checkpoint("onboarding completed", gui)
    dismiss_create_wallet_wizard(gui)

    for wallet_name in wallet_names:
        load_wallet(gui, harness, wallet_name)
    select_wallet(gui, wallet_names[0])
    gui.wait_for_property("walletBadge", "text", wallet_names[0], timeout_ms=20000)
    checkpoints.checkpoint("first wallet selected", gui)

    open_wallet_settings(gui)
    gui.click("walletPasswordRow")
    gui.wait_for_page("walletPasswordSettingsPage", timeout_ms=10000)
    checkpoints.checkpoint("password subpage opened for first wallet", gui)

    select_wallet(gui, wallet_names[1])
    gui.wait_for_page("walletSettingsPage", timeout_ms=10000)
    gui.wait_for_property("walletBadge", "text", wallet_names[1], timeout_ms=20000)
    checkpoints.checkpoint("password subpage unwound after selecting second wallet", gui)


def case_wrong_current_password_preserves_current_field(harness, checkpoints):
    wallet_name = "settings_password_clear_wallet"
    new_password = "another correct horse battery staple"

    prepare_managed_wallet(harness, wallet_name, WALLET_PASSWORD)
    checkpoints.checkpoint("managed wallet fixture prepared")

    harness.start_gui()
    gui = harness.driver
    checkpoints.checkpoint("GUI launched", gui)
    harness.finish_onboarding()
    checkpoints.checkpoint("onboarding completed", gui)
    dismiss_create_wallet_wizard(gui)
    load_wallet(gui, harness, wallet_name)
    checkpoints.checkpoint("managed wallet loaded", gui)

    open_wallet_settings(gui)
    gui.click("walletPasswordRow")
    gui.wait_for_page("walletPasswordSettingsPage", timeout_ms=10000)

    gui.set_text("walletPasswordCurrentField", "wrong password")
    gui.set_text("walletPasswordNewField", new_password)
    gui.set_text("walletPasswordConfirmField", new_password)
    gui.wait_for_property("walletPasswordSaveButton", "enabled", True, timeout_ms=5000)
    gui.click("walletPasswordSaveButton")
    gui.wait_for_property(
        "walletPasswordErrorText",
        "text",
        lambda text: "incorrect" in text.lower(),
        timeout_ms=10000,
    )
    assert gui.get_text("walletPasswordCurrentField") == "wrong password", "Current password field should be preserved after failure"
    checkpoints.checkpoint("wrong current password rejected and preserved", gui)


def run_test(args):
    screenshot_root = None
    if args.save_screenshots:
        screenshot_root = make_screenshot_root()
        print(f"Checkpoint screenshots will be saved under: {screenshot_root}")

    cases = [
        ("qml_wallet_settings_rename", 400, case_rename_persists_across_restart),
        ("qml_wallet_settings_backup", 410, case_backup_uses_automation_path),
        ("qml_wallet_settings_sign_verify_message", 420, case_sign_verify_message),
        ("qml_wallet_settings_subpage_close", 430, case_subpages_close_when_wallet_becomes_unselected),
        ("qml_wallet_settings_password_context_change", 440, case_password_page_closes_when_selected_wallet_changes),
        ("qml_wallet_settings_password_failure_preserves_current", 450, case_wrong_current_password_preserves_current_field),
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
    raise SystemExit(run_test(parse_args()))
