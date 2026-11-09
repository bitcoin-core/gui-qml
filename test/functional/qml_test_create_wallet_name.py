#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""End-to-end GUI tests for create-wallet name availability checks."""

import argparse
import os
import signal
import subprocess
import sys
import time

from qml_driver import QmlDriverError
from qml_test_harness import dump_qml_tree
from qml_wallet_test_lib import WalletFlowHarness, find_bitcoind, rpc_call, wait_for_rpc


def parse_args():
    parser = argparse.ArgumentParser(description="Create-wallet name GUI functional test")
    return parser.parse_args()


def wait_for_text_contains(gui, object_name, expected_substring, timeout_ms=10000):
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


def open_create_wallet_form(gui):
    gui.wait_for_property("walletBadge", "loading", False, timeout_ms=20000)
    gui.click("walletBadge")
    try:
        gui.wait_for_property("walletSelectPopup", "opened", True, timeout_ms=2000)
        gui.click("walletSelectAddWalletButton")
    except QmlDriverError:
        pass
    gui.wait_for_property("walletTypeRegular", "visible", True, timeout_ms=10000)
    gui.click("walletTypeRegular")
    gui.wait_for_page("createWalletFormPage", timeout_ms=10000)
    gui.set_text("createWalletPasswordInput", "test wallet password")
    gui.set_text("createWalletPasswordRepeatInput", "test wallet password")
    gui.click("createWalletPasswordConfirmToggle")


def submit_name(gui, wallet_name):
    gui.set_text("createWalletNameInput", wallet_name)
    assert gui.get_text("createWalletFormErrorText") == ""
    gui.wait_for_property("createWalletFormCreateButton", "enabled", True, timeout_ms=5000)
    gui.click("createWalletFormCreateButton")
    gui.settle()


def create_closed_wallet_fixture(harness, wallet_name):
    if harness.bitcoind_binary is None:
        harness.bitcoind_binary = find_bitcoind()
    process = subprocess.Popen(
        [harness.bitcoind_binary, f"-datadir={harness.gui_datadir}"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    try:
        wait_for_rpc(harness.gui_rpc_port)
        rpc_call(harness.gui_rpc_port, "createwallet", {"wallet_name": wallet_name})
        rpc_call(harness.gui_rpc_port, "unloadwallet", [wallet_name])
        rpc_call(harness.gui_rpc_port, "stop")
        process.wait(timeout=20)
    except Exception:
        if process.poll() is None:
            process.send_signal(signal.SIGTERM)
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        raise


def run_case(case_name, case_body):
    harness = WalletFlowHarness(case_name, port_offset=70)
    try:
        print(f"[{case_name}] starting")
        case_body(harness)
        print(f"[{case_name}] completed")
        return 0
    except Exception as err:  # noqa: BLE001 - preserve GUI state and logs
        print(f"\nFAILED [{case_name}]: {err}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        gui_output = harness.process_output(harness.gui_process)
        if gui_output:
            print("\n--- GUI process output ---", file=sys.stderr)
            print(gui_output, file=sys.stderr)
        if harness.driver is not None:
            dump_qml_tree(harness.driver)
        return 1
    finally:
        harness.stop()


def seed_wallet_display_alias(harness, wallet_path, alias):
    """Write a wallet display alias into the GUI's QSettings file before launch.

    With setDefaultFormat(IniFormat) + setPath(IniFormat, UserScope, ...)
    applied by -test-settings-dir, Qt resolves the user-scope ini file as
    "<settings-dir>/<org>/<app>.ini" where <org> comes from
    OrganizationDomain on macOS and OrganizationName on Linux. Seed both
    candidate paths so the assertion runs identically on either platform.
    """
    payload = f"[walletDisplayNames]\n{wallet_path}={alias}\n"
    for org_dir in ("BitcoinCore", "bitcoincore.org"):
        settings_dir = os.path.join(harness.config_home, org_dir)
        os.makedirs(settings_dir, exist_ok=True)
        settings_path = os.path.join(settings_dir, "BitcoinCore-App-regtest.ini")
        with open(settings_path, "a", encoding="utf-8") as fh:
            fh.write(payload)


def case_name_availability(harness):
    closed_wallet = "duplicate_closed"
    loaded_wallet = "duplicate_loaded"
    alias_target = "alias_target"
    custom_alias = "Savings"
    available_wallet = "My new wallet"

    # Pre-create wallets before GUI launch so startup skips onboarding and
    # exposes the main wallet entry point used by this flow.
    create_closed_wallet_fixture(harness, closed_wallet)
    create_closed_wallet_fixture(harness, alias_target)
    seed_wallet_display_alias(harness, alias_target, custom_alias)

    harness.start_gui()
    gui = harness.driver
    wait_for_rpc(harness.gui_rpc_port)
    rpc_call(harness.gui_rpc_port, "createwallet", {"wallet_name": loaded_wallet})

    open_create_wallet_form(gui)

    submit_name(gui, closed_wallet)
    gui.wait_for_page("createWalletFormPage", timeout_ms=10000)
    wait_for_text_contains(gui, "createWalletFormErrorText", "already exists")

    submit_name(gui, loaded_wallet)
    gui.wait_for_page("createWalletFormPage", timeout_ms=10000)
    wait_for_text_contains(gui, "createWalletFormErrorText", "already exists")

    # Reject names that collide with another wallet's stored display alias.
    submit_name(gui, custom_alias)
    gui.wait_for_page("createWalletFormPage", timeout_ms=10000)
    wait_for_text_contains(gui, "createWalletFormErrorText", "already exists")

    # Case-insensitive collision.
    submit_name(gui, custom_alias.lower())
    gui.wait_for_page("createWalletFormPage", timeout_ms=10000)
    wait_for_text_contains(gui, "createWalletFormErrorText", "already exists")

    submit_name(gui, available_wallet)
    gui.wait_for_page("walletCreationReadyPage", timeout_ms=20000)
    gui.click("createWalletReadyDoneButton")
    gui.wait_for_property("walletBadge", "text", available_wallet, timeout_ms=20000)


def run_test(_args):
    return run_case("qml_create_wallet_name_availability", case_name_availability)


if __name__ == "__main__":
    sys.exit(run_test(parse_args()))
