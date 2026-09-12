#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""End-to-end GUI tests for the wallet selector's load flow."""

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
    rpc_call,
    wait_for_rpc,
)


def start_node(bitcoind_binary, datadir, rpc_port):
    process = subprocess.Popen(
        [bitcoind_binary, f"-datadir={datadir}"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    wait_for_rpc(rpc_port)
    return process


def stop_node(process, rpc_port):
    if process is None or process.poll() is not None:
        return
    try:
        rpc_call(rpc_port, "stop")
    except Exception:
        process.send_signal(signal.SIGTERM)
    try:
        process.wait(timeout=20)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()


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


# Mirror of WalletListModel::LoadState — keep in sync with
# qml/models/walletlistmodel.h.
LOAD_STATE_CLOSED = 0
LOAD_STATE_OPEN = 1
LOAD_STATE_LOADING = 2
LOAD_STATE_LOAD_ERROR = 3


def parse_args():
    parser = argparse.ArgumentParser(
        description="Wallet selector load-flow GUI functional test",
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
    screenshot_root = os.path.join(artifacts_root, f"qml_test_wallet_selector_load-{timestamp}")
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


def precreate_closed_wallet(harness, wallet_name):
    """Create a wallet on the GUI's datadir via a temporary bitcoind, then stop it.
    The wallet files remain on disk but no wallet is loaded when the GUI launches."""
    process = start_node(harness.bitcoind_binary, harness.gui_datadir, harness.gui_rpc_port)
    try:
        rpc_call(harness.gui_rpc_port, "createwallet", {"wallet_name": wallet_name})
    finally:
        stop_node(process, harness.gui_rpc_port)


def open_wallet_selector(gui):
    gui.wait_for_property("walletBadge", "loading", False, timeout_ms=20000)
    if gui.get_property("walletSelectPopup", "opened") is True:
        return
    gui.click("walletBadge")
    gui.wait_for_property("walletSelectPopup", "opened", True, timeout_ms=5000)


def case_selector_loads_closed_wallet(harness, checkpoints):
    """Happy path: a closed wallet on disk is listed, clicking it loads it,
    the selector auto-closes when the load completes, the badge reflects the
    newly opened wallet, and the row re-renders with the open state on reopen."""

    wallet_name = "selector_load_target"
    row_object = f"walletSelectItem_{sanitize_object_suffix(wallet_name)}"
    status_object = f"walletSelectStatus_{sanitize_object_suffix(wallet_name)}"

    from qml_wallet_test_lib import find_bitcoind  # local import to avoid global side effects
    harness.bitcoind_binary = find_bitcoind()
    precreate_closed_wallet(harness, wallet_name)
    checkpoints.checkpoint("closed wallet prepared on disk")

    harness.start_gui()
    gui = harness.driver
    checkpoints.checkpoint("GUI launched", gui)

    harness.finish_onboarding()
    # On a fresh GUI with no wallet loaded the create-wallet wizard appears;
    # close it so we land on the wallet overview with the badge.
    dismiss_create_wallet_wizard(gui)
    wait_for_wallet_ready(harness, gui)

    # Badge should reflect "no wallet loaded" since we never loaded the wallet.
    assert gui.get_property("walletBadge", "noWalletLoaded") is True, (
        "Expected no wallet loaded at GUI start"
    )
    checkpoints.checkpoint("no-wallet-loaded badge displayed", gui)

    # Open the wallet selector — the closed wallet should appear in the list.
    open_wallet_selector(gui)
    gui.settle()

    # The row exists and reports Closed.
    closed_state = gui.get_property(row_object, "loadState")
    assert closed_state == LOAD_STATE_CLOSED, (
        f"Expected loadState=Closed ({LOAD_STATE_CLOSED}) before click, got {closed_state}"
    )
    closed_text = gui.get_text(status_object)
    assert not closed_text, f"Expected no status for a closed wallet, got {closed_text!r}"
    # The selector popup must be open before the click — confirms our test setup.
    assert gui.get_property("walletSelectPopup", "opened") is True
    checkpoints.checkpoint("closed wallet listed in selector", gui)

    # Click the row to start the load. The selector should stay open until
    # the load resolves, then auto-close on success.
    gui.click(row_object)

    # The popup must auto-close once walletLoadSucceeded fires.
    gui.wait_for_property("walletSelectPopup", "opened", False, timeout_ms=20000)
    checkpoints.checkpoint("selector auto-closed on load success", gui)

    # Badge picks up the wallet name once loaded.
    gui.wait_for_property("walletBadge", "text", wallet_name, timeout_ms=20000)
    wait_for_wallet_ready(harness, gui)
    assert gui.get_property("walletBadge", "noWalletLoaded") is False, (
        "Expected a wallet to be loaded after selector click"
    )
    checkpoints.checkpoint("badge reflects newly loaded wallet", gui)

    # Reopen the selector — the wallet now reports Open with a balance and unit.
    open_wallet_selector(gui)
    gui.settle()
    open_state = gui.get_property(row_object, "loadState")
    assert open_state == LOAD_STATE_OPEN, (
        f"Expected loadState=Open ({LOAD_STATE_OPEN}) after load, got {open_state}"
    )
    open_text = gui.get_text(status_object)
    assert open_text.startswith("₿ "), (
        f"Expected status line to start with '₿ ' once loaded, got {open_text!r}"
    )
    checkpoints.checkpoint("loaded wallet shows balance in selector", gui)


def plant_readonly_wallet(harness, wallet_name):
    """Create a real wallet via a temporary bitcoind, then strip write
    permissions so the GUI's loadWallet() fails gracefully on the SQLite
    open. listWalletDir() still surfaces the directory; the load attempt
    fails with a permission error — exercising the LoadError UI path
    without tripping bitcoind's internal asserts."""
    from qml_wallet_test_lib import find_bitcoind
    if harness.bitcoind_binary is None:
        harness.bitcoind_binary = find_bitcoind()
    precreate_closed_wallet(harness, wallet_name)
    wallet_dir = os.path.join(harness.gui_datadir, "regtest", "wallets", wallet_name)
    db_path = os.path.join(wallet_dir, "wallet.dat")
    os.chmod(db_path, 0o444)
    os.chmod(wallet_dir, 0o555)


def case_selector_surfaces_load_error(harness, checkpoints):
    """Failure path: a wallet that listWalletDir() recognizes but loadWallet
    rejects must surface the error inline (LoadError row state, status text
    in red) AND open the modal alert popup. Dismissing the popup leaves the
    inline error in place until the user takes the next action."""

    wallet_name = "selector_load_error_target"
    row_object = f"walletSelectItem_{sanitize_object_suffix(wallet_name)}"
    status_object = f"walletSelectStatus_{sanitize_object_suffix(wallet_name)}"

    plant_readonly_wallet(harness, wallet_name)
    checkpoints.checkpoint("read-only wallet planted on disk")

    harness.start_gui()
    gui = harness.driver
    harness.finish_onboarding()
    dismiss_create_wallet_wizard(gui)
    wait_for_wallet_ready(harness, gui)

    open_wallet_selector(gui)
    gui.settle()

    closed_state = gui.get_property(row_object, "loadState")
    assert closed_state == LOAD_STATE_CLOSED, (
        f"Expected loadState=Closed before click, got {closed_state}"
    )
    checkpoints.checkpoint("read-only wallet listed as Closed", gui)

    # Click — the load must fail, the alert popup must open, and the row
    # must transition to LoadError. The selector stays open underneath
    # (per design: only success/migration auto-close the selector).
    gui.click(row_object)
    gui.wait_for_property("walletLoadErrorPopup", "opened", True, timeout_ms=15000)
    error_text = gui.get_text("walletLoadErrorPopupText")
    assert error_text, "Load error popup should display a non-empty error message"
    checkpoints.checkpoint("load error alert popup displayed", gui)

    error_state = gui.get_property(row_object, "loadState")
    assert error_state == LOAD_STATE_LOAD_ERROR, (
        f"Expected loadState=LoadError ({LOAD_STATE_LOAD_ERROR}) after failed load, "
        f"got {error_state}"
    )
    failed_status = gui.get_text(status_object)
    assert "Failed" in failed_status or "failed" in failed_status, (
        f"Expected status line to indicate failure, got {failed_status!r}"
    )

    # Dismiss the popup — inline LoadError state should persist on the row
    # (it only resets on the next user action, e.g. selecting a different
    # wallet or starting a fresh load).
    gui.click("walletLoadErrorPopupDismissButton")
    gui.wait_for_property("walletLoadErrorPopup", "opened", False, timeout_ms=5000)
    persisted_state = gui.get_property(row_object, "loadState")
    assert persisted_state == LOAD_STATE_LOAD_ERROR, (
        f"Expected LoadError to persist after popup dismiss, got {persisted_state}"
    )
    checkpoints.checkpoint("inline LoadError persists after popup dismiss", gui)


def case_selector_skips_load_for_already_open_wallet(harness, checkpoints):
    """Selecting a wallet that is already in the open set must not trigger a
    fresh load attempt — the selector closes synchronously and the badge
    updates without the row ever showing the Loading state."""

    wallet_a = "already_open_a"
    wallet_b = "already_open_b"
    row_a = f"walletSelectItem_{sanitize_object_suffix(wallet_a)}"

    from qml_wallet_test_lib import find_bitcoind
    harness.bitcoind_binary = find_bitcoind()

    process = start_node(harness.bitcoind_binary, harness.gui_datadir, harness.gui_rpc_port)
    try:
        for name in (wallet_a, wallet_b):
            rpc_call(harness.gui_rpc_port, "createwallet", {"wallet_name": name, "disable_private_keys": name == wallet_b})
    finally:
        stop_node(process, harness.gui_rpc_port)
    checkpoints.checkpoint("two managed wallets prepared")

    harness.start_gui()
    gui = harness.driver
    harness.finish_onboarding()
    dismiss_create_wallet_wizard(gui)
    wait_for_wallet_ready(harness, gui)

    # Load both wallets via RPC so they become Open in the model.
    for name in (wallet_a, wallet_b):
        rpc_call(harness.gui_rpc_port, "loadwallet", [name])
    gui.wait_for_property("walletBadge", "text", wallet_b, timeout_ms=20000)
    checkpoints.checkpoint("both wallets open via RPC", gui)

    # Select wallet A from the selector — it's already in the open set,
    # so this should be a synchronous selection swap with no Loading state.
    open_wallet_selector(gui)
    gui.settle()
    assert gui.get_property(row_a, "loadState") == LOAD_STATE_OPEN, (
        "Wallet A should already be Open before selection"
    )
    gui.click(row_a)
    # Popup should close immediately (the synchronous already-open branch).
    gui.wait_for_property("walletSelectPopup", "opened", False, timeout_ms=2000)
    gui.wait_for_property("walletBadge", "text", wallet_a, timeout_ms=5000)
    checkpoints.checkpoint("already-open wallet selected synchronously", gui)

    # Both wallets remain in the open set; the row state must still be Open.
    open_wallet_selector(gui)
    gui.settle()
    assert gui.get_property(row_a, "loadState") == LOAD_STATE_OPEN
    loaded = rpc_call(harness.gui_rpc_port, "listwallets")
    assert set(loaded) == {wallet_a, wallet_b}, f"Unexpected open set: {loaded}"

    # Actions target their own row without selecting it until Settings is chosen.
    suffix_b = sanitize_object_suffix(wallet_b)
    gui.click(f"walletSelectActions_{suffix_b}")
    gui.wait_for_object(f"walletSelectSettings_{suffix_b}", timeout_ms=5000)
    assert gui.get_property("walletBadge", "text") == wallet_a
    gui.click(f"walletSelectSettings_{suffix_b}")
    gui.wait_for_property("walletBadge", "text", wallet_b, timeout_ms=5000)
    gui.wait_for_property("desktopWalletSettingsTabButton", "checked", True, timeout_ms=5000)
    checkpoints.checkpoint("row actions open the correct wallet settings", gui)

    open_wallet_selector(gui)
    gui.click(f"walletSelectActions_{suffix_b}")
    gui.click(f"walletSelectClose_{suffix_b}")
    gui.wait_for_property("walletCloseConfirmationPopup", "opened", True, timeout_ms=5000)
    assert set(rpc_call(harness.gui_rpc_port, "listwallets")) == {wallet_a, wallet_b}
    gui.click("walletCloseConfirmationCancelButton")
    assert set(rpc_call(harness.gui_rpc_port, "listwallets")) == {wallet_a, wallet_b}

    open_wallet_selector(gui)
    gui.click(f"walletSelectActions_{suffix_b}")
    gui.click(f"walletSelectClose_{suffix_b}")
    gui.wait_for_property("walletCloseConfirmationPopup", "opened", True, timeout_ms=5000)
    gui.click("walletCloseConfirmationConfirmButton")
    gui.wait_for_property("walletBadge", "text", wallet_a, timeout_ms=5000)
    assert rpc_call(harness.gui_rpc_port, "listwallets") == [wallet_a]
    checkpoints.checkpoint("selected wallet unloads only after confirmation", gui)
    open_wallet_selector(gui)
    row_b = f"walletSelectItem_{suffix_b}"
    assert gui.get_property(row_b, "loadState") == LOAD_STATE_CLOSED
    assert gui.get_property(row_b, "iconSource") == "image://images/wallet"
    gui.wait_for_property(f"walletSelectActionsMenu_{suffix_b}", "visible", False, timeout_ms=5000)
    checkpoints.checkpoint("closed wallet uses generic wallet icon", gui)



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
            except Exception as screenshot_err:  # noqa: BLE001
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


def run_test(args):
    screenshot_root = None
    if args.save_screenshots:
        screenshot_root = make_screenshot_root()
        print(f"Checkpoint screenshots will be saved under: {screenshot_root}")

    cases = [
        ("qml_wallet_selector_load_happy_path", 400, case_selector_loads_closed_wallet),
        ("qml_wallet_selector_load_error", 410, case_selector_surfaces_load_error),
        ("qml_wallet_selector_already_open", 420, case_selector_skips_load_for_already_open_wallet),
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
