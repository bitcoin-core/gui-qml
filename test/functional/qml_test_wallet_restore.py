#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""End-to-end GUI tests for wallet restore failures and success."""

import argparse
import os
import re
import sys
import time
from datetime import datetime

from qml_driver import QmlDriverError
from qml_test_harness import dump_qml_tree
from qml_wallet_test_lib import WalletFlowHarness, find_legacy_bitcoind, rpc_call


def parse_args():
    parser = argparse.ArgumentParser(
        description="Wallet restore GUI functional test",
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
    screenshot_root = os.path.join(artifacts_root, f"qml_test_wallet_restore-{timestamp}")
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


def import_wallet_from_type_selector(gui, backup_path):
    gui.wait_for_property("walletBadge", "loading", False, timeout_ms=20000)
    assert gui.get_property("walletBadge", "noWalletLoaded") is True, "Expected no wallet to be loaded at startup"

    gui.click("walletBadge")
    try:
        gui.wait_for_property("walletSelectPopup", "opened", True, timeout_ms=2000)
        gui.click("walletSelectAddWalletButton")
    except QmlDriverError:
        pass
    gui.wait_for_property("walletTypeImport", "visible", True, timeout_ms=10000)
    gui.set_text("importWalletPathField", backup_path)
    gui.click("walletTypeImport")


def wait_for_text_contains(gui, object_name, expected_substring, timeout_ms=20000):
    deadline = time.time() + (timeout_ms / 1000)
    last_text = ""
    while time.time() < deadline:
        last_text = gui.get_text(object_name)
        if expected_substring in last_text:
            return last_text
        time.sleep(0.25)
    raise AssertionError(
        f"Expected {object_name} text to contain {expected_substring!r}, got {last_text!r}"
    )


def run_case(case_name, port_offset, case_body, save_screenshots=False, screenshot_root=None):
    harness = WalletFlowHarness(case_name, port_offset=port_offset)
    checkpoints = CheckpointRecorder(case_name, save_screenshots, screenshot_root)
    gui = None
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


def case_bad_format(harness, checkpoints):
    backup_path = os.path.join(os.path.dirname(__file__), "fixtures", "invalid-wallet-backup.bak")
    checkpoints.checkpoint("bad-format fixture located")

    harness.start_gui()
    gui = harness.driver
    checkpoints.checkpoint("GUI launched", gui)
    import_wallet_from_type_selector(gui, backup_path)
    checkpoints.checkpoint("bad-format backup submitted", gui)

    gui.wait_for_property("importWalletErrorView", "visible", True, timeout_ms=20000)
    checkpoints.checkpoint("bad-format error screen displayed", gui)

    gui.wait_for_property("createWalletWizard", "currentItem.objectName", "walletImportErrorPage", timeout_ms=20000)
    assert gui.get_property("walletBadge", "noWalletLoaded") is True, "Bad-format restore should not load a wallet"
    assert gui.get_text("importWalletErrorTitle") == "This wallet type is not supported"
    assert gui.get_text("importWalletErrorDescription") == (
        "The file looks like a wallet backup, but this application could not verify or open it in a supported format."
    )
    help_text = wait_for_text_contains(gui, "importWalletErrorHelpText", "Data is not in recognized format.")

    restored_wallet_path = os.path.join(harness.gui_wallets_path, "bad_format")
    assert not os.path.exists(restored_wallet_path), f"Unexpected wallet directory left behind: {restored_wallet_path}"
    print(f"Bad-format restore failure surfaced expected help text: {help_text}")


def case_legacy_wallet(harness, checkpoints):
    legacy_binary = find_legacy_bitcoind()
    if not legacy_binary:
        print("SKIPPED [qml_wallet_restore_legacy_failure]: legacy bitcoind not found.")
        print("Set BITCOIND_LEGACY or provide releases/v28.0/bin/bitcoind to exercise this flow.")
        return
    checkpoints.checkpoint("legacy fixture binary located")

    wallet_name = "legacy_import"
    backup_path = os.path.join(harness.tmpdir, f"{wallet_name}.bak")

    harness.start_source_node(
        binary=legacy_binary,
        extra_args=["-deprecatedrpc=create_bdb"],
    )
    try:
        rpc_call(
            harness.source_rpc_port,
            "createwallet",
            {
                "wallet_name": wallet_name,
                "descriptors": False,
            },
        )
    except RuntimeError as err:
        print(f"SKIPPED [qml_wallet_restore_legacy_failure]: could not create a legacy wallet fixture: {err}")
        return

    rpc_call(harness.source_rpc_port, "backupwallet", [backup_path], wallet=wallet_name)
    harness.stop_source_node()
    checkpoints.checkpoint("legacy backup fixture created")

    harness.start_gui()
    gui = harness.driver
    checkpoints.checkpoint("GUI launched", gui)
    import_wallet_from_type_selector(gui, backup_path)
    checkpoints.checkpoint("legacy backup submitted", gui)

    gui.wait_for_property("importWalletErrorView", "visible", True, timeout_ms=20000)
    checkpoints.checkpoint("legacy-wallet error screen displayed", gui)

    gui.wait_for_property("createWalletWizard", "currentItem.objectName", "walletImportErrorPage", timeout_ms=20000)
    assert gui.get_property("walletBadge", "noWalletLoaded") is True, "Legacy restore failure should not load a wallet"
    assert gui.get_text("importWalletErrorTitle") == "This wallet needs to be migrated"
    assert gui.get_text("importWalletErrorDescription") == (
        "The selected backup appears to come from a legacy wallet format. It needs to be migrated before it can be used here."
    )
    assert gui.get_text("importWalletErrorHelpText") == (
        "If this is a legacy wallet backup, migrate it with the wallet migration tool before importing it here."
    )

    restored_wallet_path = os.path.join(harness.gui_wallets_path, wallet_name)
    assert not os.path.exists(restored_wallet_path), f"Unexpected wallet directory left behind: {restored_wallet_path}"
    print("Legacy restore failure surfaced expected migration guidance.")


def case_successful_import(harness, checkpoints):
    harness.start_source_node()
    wallet_name = "restore_source"
    backup_path = os.path.join(harness.tmpdir, f"{wallet_name}.bak")
    rpc_call(harness.source_rpc_port, "createwallet", {"wallet_name": wallet_name})
    rpc_call(harness.source_rpc_port, "backupwallet", [backup_path], wallet=wallet_name)
    harness.stop_source_node()
    checkpoints.checkpoint("valid backup fixture created")

    harness.start_gui()
    gui = harness.driver
    checkpoints.checkpoint("GUI launched", gui)
    import_wallet_from_type_selector(gui, backup_path)
    checkpoints.checkpoint("valid backup submitted", gui)

    gui.wait_for_page("importWalletSuccessPage", timeout_ms=20000)
    checkpoints.checkpoint("import success screen displayed", gui)

    assert gui.get_property("importWalletSuccessWalletName", "text") == wallet_name
    assert gui.get_property("importWalletSuccessKeyScheme", "text") == "Single-key"
    gui.click("importWalletSuccessOverviewButton")

    gui.wait_for_property("walletBadge", "text", wallet_name, timeout_ms=20000)
    checkpoints.checkpoint("wallet overview reopened", gui)
    restored_wallet_path = os.path.join(harness.gui_wallets_path, wallet_name)
    assert os.path.isdir(restored_wallet_path), f"Expected restored wallet directory at {restored_wallet_path}"
    print("Restore success flow passed.")


def run_test(args):
    screenshot_root = None
    if args.save_screenshots:
        screenshot_root = make_screenshot_root()
        print(f"Checkpoint screenshots will be saved under: {screenshot_root}")

    if run_case(
        "qml_wallet_restore_bad_format",
        port_offset=40,
        case_body=case_bad_format,
        save_screenshots=args.save_screenshots,
        screenshot_root=screenshot_root,
    ) != 0:
        return 1
    if run_case(
        "qml_wallet_restore_legacy_failure",
        port_offset=50,
        case_body=case_legacy_wallet,
        save_screenshots=args.save_screenshots,
        screenshot_root=screenshot_root,
    ) != 0:
        return 1
    if run_case(
        "qml_wallet_restore_success",
        port_offset=60,
        case_body=case_successful_import,
        save_screenshots=args.save_screenshots,
        screenshot_root=screenshot_root,
    ) != 0:
        return 1

    print("Wallet restore flows passed.")
    return 0


if __name__ == "__main__":
    sys.exit(run_test(parse_args()))
