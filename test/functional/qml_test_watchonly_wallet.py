#!/usr/bin/env python3
# Copyright (c) 2025 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""End-to-end GUI tests for watch-only wallet creation flow."""

import re
import sys

from qml_driver import QmlDriverError
from qml_test_harness import dump_qml_tree
from qml_wallet_test_lib import WalletFlowHarness, rpc_call


def open_create_wallet_page(gui):
    """Navigate from the post-onboarding main screen to the Create Wallet wizard."""
    gui.wait_for_property("walletBadge", "loading", False, timeout_ms=20000)

    gui.click("walletBadge")
    try:
        gui.wait_for_property("walletSelectPopup", "opened", True, timeout_ms=2000)
        gui.click("walletSelectAddWalletButton")
    except QmlDriverError:
        pass
    gui.wait_for_page("walletCreationTypePage", timeout_ms=10000)


def open_type_selector(gui):
    """Navigate to the wallet type selector page."""
    open_create_wallet_page(gui)


def get_test_xpub(harness):
    """Create a wallet on the source node and extract its account-level xpub."""
    harness.start_source_node()
    rpc_call(harness.source_rpc_port, "createwallet", {"wallet_name": "source", "descriptors": True})
    result = rpc_call(harness.source_rpc_port, "listdescriptors", wallet="source")

    xpub = None
    for desc_info in result["descriptors"]:
        desc = desc_info["desc"]
        if desc.startswith("wpkh(") and not desc_info.get("internal", False):
            match = re.search(r'([tx]pub[A-Za-z0-9]+)', desc)
            if match:
                xpub = match.group(1)
                break

    harness.stop_source_node()
    if not xpub:
        raise RuntimeError("Could not extract xpub from source wallet")
    return xpub


def case_watchonly_creation_flow(harness, test_xpub):
    """Test the full watch-only wallet creation flow."""
    harness.start_gui()
    gui = harness.driver

    open_type_selector(gui)
    print("  Type selector opened")

    gui.click("walletTypeViewOnly")
    gui.wait_for_page("createWalletFormPage", timeout_ms=5000)
    gui.set_text("createWalletNameInput", "watchonly_test")
    gui.set_text("watchOnlyXpubInput", test_xpub)
    gui.wait_for_property("createWalletFormCreateButton", "enabled", True, timeout_ms=5000)
    gui.click("createWalletFormCreateButton")
    gui.wait_for_page("walletCreationReadyPage", timeout_ms=15000)
    print("  Wallet created, ready page shown")

    gui.click("createWalletReadyDoneButton")
    gui.wait_for_property("walletBadge", "loading", False, timeout_ms=15000)
    print("  Ready page dismissed")

    badge_text = gui.get_text("walletBadge")
    print(f"  Wallet badge: {badge_text}")

    wallet_info = rpc_call(harness.gui_rpc_port, "getwalletinfo", wallet="watchonly_test")
    assert wallet_info["private_keys_enabled"] is False, \
        f"Wallet should be watch-only, got private_keys_enabled={wallet_info['private_keys_enabled']}"
    print("  RPC confirms wallet is watch-only (private_keys_enabled=false)")

    print("  Watch-only creation flow PASSED")


def case_type_selector_regular_flow(harness):
    """Test that regular wallet flow still works through type selector."""
    harness.start_gui()
    gui = harness.driver

    open_type_selector(gui)
    print("  Type selector opened")

    gui.click("walletTypeRegular")
    gui.wait_for_page("createWalletFormPage", timeout_ms=5000)
    print("  Regular wallet form opened")

    print("  Regular wallet type selector flow PASSED")


def case_type_selector_disabled_options(harness):
    """Test that unavailable wallet types are omitted."""
    harness.start_gui()
    gui = harness.driver

    open_type_selector(gui)
    print("  Type selector opened")

    assert not gui.object_exists("walletTypeMultiKey"), "Multi-key option should not be shown"
    assert not gui.object_exists("walletTypeCustom"), "Custom option should not be shown"

    print("  Disabled options check PASSED")


def run_case(case_name, port_offset, case_body):
    harness = WalletFlowHarness(case_name, port_offset=port_offset)
    try:
        print(f"[{case_name}] starting")
        case_body(harness)
        print(f"[{case_name}] completed")
        return 0
    except Exception as err:
        print(f"\nFAILED [{case_name}]: {err}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        gui = harness.driver
        if gui is not None:
            dump_qml_tree(gui)
        gui_output = harness.process_output(harness.gui_process)
        if gui_output:
            print("\n--- GUI process output ---", file=sys.stderr)
            print(gui_output, file=sys.stderr)
        return 1
    finally:
        harness.stop()


def run_tests():
    print("Generating test xpub from source node...")
    xpub_harness = WalletFlowHarness("xpub_gen", port_offset=70)
    try:
        test_xpub = get_test_xpub(xpub_harness)
        print(f"  Test xpub: {test_xpub[:20]}...{test_xpub[-10:]}")
    finally:
        xpub_harness.stop()

    failures = 0

    failures += run_case(
        "watchonly_creation_flow",
        port_offset=80,
        case_body=lambda h: case_watchonly_creation_flow(h, test_xpub),
    )

    failures += run_case(
        "type_selector_regular",
        port_offset=90,
        case_body=case_type_selector_regular_flow,
    )

    failures += run_case(
        "type_selector_disabled",
        port_offset=110,
        case_body=case_type_selector_disabled_options,
    )

    if failures > 0:
        print(f"\n{failures} test case(s) FAILED", file=sys.stderr)
        return 1

    print("\nAll watch-only wallet tests PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(run_tests())
