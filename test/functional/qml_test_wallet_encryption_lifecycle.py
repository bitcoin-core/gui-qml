#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""End-to-end GUI tests for the wallet encryption lifecycle."""

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
    find_bitcoind,
    open_wallet_selector,
    rpc_call,
    wait_for_loaded_wallets,
    wait_for_rpc,
)

WALLET_PASSWORD = "correct horse battery staple"
NEW_PASSWORD = "new lifecycle password 123"


def parse_args():
    parser = argparse.ArgumentParser(
        description="Wallet encryption lifecycle GUI functional test",
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
    screenshot_root = os.path.join(artifacts_root, f"qml_test_wallet_encryption_lifecycle-{timestamp}")
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
        gui.settle(timeout_ms=15000)
        if not self.save_screenshots:
            return
        case_dir = os.path.join(self.screenshot_root, self.case_name)
        os.makedirs(case_dir, exist_ok=True)
        filename = f"{self.index:02d}-{self._sanitize_label(label)}.png"
        screenshot_path = os.path.join(case_dir, filename)
        screenshot = gui.save_screenshot(screenshot_path)
        print(f"{prefix}: screenshot saved to {screenshot['path']}")


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


def wait_for_wallet_ready(harness, gui):
    wait_for_rpc(harness.gui_rpc_port, timeout=60)
    gui.wait_for_property("walletBadge", "loading", False, timeout_ms=25000)


def load_wallet(gui, harness, wallet_name):
    wait_for_rpc(harness.gui_rpc_port, timeout=60)
    rpc_call(harness.gui_rpc_port, "loadwallet", [wallet_name])
    wait_for_wallet_ready(harness, gui)


def open_wallet_settings(gui):
    gui.click("desktopWalletSettingsTabButton")
    gui.settle()
    gui.wait_for_property("settingsSidebar_wallet", "visible", True, timeout_ms=5000)
    gui.click("settingsSidebar_wallet")
    gui.wait_for_page("walletSettingsPage", timeout_ms=10000)


def open_password_settings(gui):
    open_wallet_settings(gui)
    gui.click("walletPasswordRow")
    gui.wait_for_page("walletPasswordSettingsPage", timeout_ms=10000)


def wallet_is_encrypted(port, wallet_name):
    info = rpc_call(port, "getwalletinfo", wallet=wallet_name)
    return "unlocked_until" in info


def try_passphrase(port, wallet_name, passphrase):
    try:
        rpc_call(port, "walletpassphrase", [passphrase, 5], wallet=wallet_name)
        rpc_call(port, "walletlock", wallet=wallet_name)
        return True
    except Exception:
        return False


def run_case(case_name, port_offset, case_body, save_screenshots=False, screenshot_root=None):
    harness = WalletFlowHarness(case_name, port_offset=port_offset)
    checkpoints = CheckpointRecorder(case_name, save_screenshots, screenshot_root)
    try:
        print(f"[{case_name}] starting")
        case_body(harness, checkpoints)
        print(f"[{case_name}] completed")
        return 0
    except Exception as err:  # noqa: BLE001
        print(f"\nFAILED [{case_name}]: {err}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        gui = harness.driver
        if gui is not None:
            try:
                checkpoints.checkpoint("failure state", gui)
            except Exception as screenshot_err:  # noqa: BLE001
                print(f"[{case_name}] failed to save failure screenshot: {screenshot_err}", file=sys.stderr)
        if gui is not None:
            dump_qml_tree(gui)
        return 1
    finally:
        harness.stop()


def case_set_password(harness, checkpoints):
    wallet_name = "encryption_set_wallet"

    prepare_managed_wallet(harness, wallet_name, "")
    checkpoints.checkpoint("unencrypted wallet fixture prepared")

    harness.start_gui()
    gui = harness.driver
    checkpoints.checkpoint("GUI launched", gui)
    harness.finish_onboarding()
    checkpoints.checkpoint("onboarding completed", gui)
    load_wallet(gui, harness, wallet_name)
    checkpoints.checkpoint("unencrypted wallet loaded", gui)

    assert not wallet_is_encrypted(harness.gui_rpc_port, wallet_name)

    open_password_settings(gui)
    checkpoints.checkpoint("password settings opened", gui)

    gui.wait_for_property("walletPasswordSaveButton", "enabled", False, timeout_ms=5000)

    # Mismatched passwords keep save disabled.
    gui.set_text("walletPasswordNewField", "first-password")
    gui.set_text("walletPasswordConfirmField", "second-password")
    gui.settle()
    assert gui.get_property("walletPasswordSaveButton", "enabled") is False

    # Matching passwords still require acknowledgement.
    gui.set_text("walletPasswordNewField", WALLET_PASSWORD)
    gui.set_text("walletPasswordConfirmField", WALLET_PASSWORD)
    gui.settle()
    assert gui.get_property("walletPasswordSaveButton", "enabled") is False
    gui.click("walletPasswordConfirmToggle")
    gui.settle()
    gui.wait_for_property("walletPasswordSaveButton", "enabled", True, timeout_ms=5000)

    gui.click("walletPasswordSaveButton")
    gui.wait_for_page("walletSettingsPage", timeout_ms=10000)
    checkpoints.checkpoint("password set", gui)

    assert wallet_is_encrypted(harness.gui_rpc_port, wallet_name)
    assert try_passphrase(harness.gui_rpc_port, wallet_name, WALLET_PASSWORD)
    assert not try_passphrase(harness.gui_rpc_port, wallet_name, "wrong-password")

    # Reopening should now be in update mode.
    open_password_settings(gui)
    gui.wait_for_property("walletPasswordCurrentEntry", "visible", True, timeout_ms=5000)
    checkpoints.checkpoint("update mode after encryption", gui)


def case_update_password(harness, checkpoints):
    wallet_name = "encryption_update_wallet"

    prepare_managed_wallet(harness, wallet_name, WALLET_PASSWORD)
    checkpoints.checkpoint("encrypted wallet fixture prepared")

    harness.start_gui()
    gui = harness.driver
    checkpoints.checkpoint("GUI launched", gui)
    harness.finish_onboarding()
    checkpoints.checkpoint("onboarding completed", gui)
    load_wallet(gui, harness, wallet_name)
    checkpoints.checkpoint("encrypted wallet loaded", gui)

    assert wallet_is_encrypted(harness.gui_rpc_port, wallet_name)

    open_password_settings(gui)
    gui.wait_for_property("walletPasswordCurrentEntry", "visible", True, timeout_ms=5000)
    checkpoints.checkpoint("password settings opened", gui)

    # Wrong current password surfaces an error and keeps old password valid.
    gui.set_text("walletPasswordCurrentField", "definitely-wrong")
    gui.set_text("walletPasswordNewField", NEW_PASSWORD)
    gui.set_text("walletPasswordConfirmField", NEW_PASSWORD)
    gui.settle()
    gui.wait_for_property("walletPasswordSaveButton", "enabled", True, timeout_ms=5000)
    gui.click("walletPasswordSaveButton")
    gui.wait_for_property("walletPasswordResultBanner", "visible", True, timeout_ms=5000)
    assert gui.get_text("walletPasswordErrorText") != ""
    assert try_passphrase(harness.gui_rpc_port, wallet_name, WALLET_PASSWORD)
    checkpoints.checkpoint("wrong current password rejected", gui)

    # Mismatched new passwords keep save disabled.
    gui.set_text("walletPasswordCurrentField", WALLET_PASSWORD)
    gui.set_text("walletPasswordNewField", NEW_PASSWORD)
    gui.set_text("walletPasswordConfirmField", NEW_PASSWORD + "-mismatch")
    gui.settle()
    assert gui.get_property("walletPasswordSaveButton", "enabled") is False

    # Successful update.
    gui.set_text("walletPasswordConfirmField", NEW_PASSWORD)
    gui.settle()
    gui.wait_for_property("walletPasswordSaveButton", "enabled", True, timeout_ms=5000)
    gui.click("walletPasswordSaveButton")
    gui.wait_for_property("walletPasswordResultBanner", "visible", True, timeout_ms=10000)
    assert "successfully" in gui.get_text("walletPasswordErrorText").lower()
    checkpoints.checkpoint("password updated", gui)

    assert not try_passphrase(harness.gui_rpc_port, wallet_name, WALLET_PASSWORD)
    assert try_passphrase(harness.gui_rpc_port, wallet_name, NEW_PASSWORD)


def main():
    args = parse_args()
    screenshot_root = make_screenshot_root() if args.save_screenshots else None

    cases = [
        ("encryption_set_password", 10, case_set_password),
        ("encryption_update_password", 20, case_update_password),
    ]

    failures = 0
    for name, offset, body in cases:
        failures += run_case(name, offset, body, args.save_screenshots, screenshot_root)

    if failures:
        print(f"\n{failures} case(s) failed.", file=sys.stderr)
        sys.exit(1)
    print("\nWallet encryption lifecycle checks passed.")


if __name__ == "__main__":
    main()
