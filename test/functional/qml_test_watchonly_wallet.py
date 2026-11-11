#!/usr/bin/env python3
# Copyright (c) 2025 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""End-to-end GUI tests for watch-only wallet creation flow."""

import hashlib
import json
import os
import re
import sys

from qml_driver import QmlDriverError
from qml_test_harness import dump_qml_tree
from qml_wallet_test_lib import WalletFlowHarness, rpc_call, wait_for_rpc


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
    gui.wait_for_property("watchOnlyAddressType", "visible", True, timeout_ms=5000)
    gui.click("watchOnlyBirth_month")
    assert gui.get_property("createWalletFormCreateButton", "enabled") is False
    gui.click("watchOnlyAddressType_bech32")
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


def fund_hot_wallet(harness):
    """Fund a wallet on the GUI node with Taproot and SegWit payments, before any view-only import."""
    port = harness.gui_rpc_port
    wait_for_rpc(port, timeout=60)
    harness.driver.wait_for_property("walletBadge", "loading", False, timeout_ms=20000)
    rpc_call(port, "createwallet", {"wallet_name": "hot", "load_on_startup": False})
    taproot = rpc_call(port, "getnewaddress", ["", "bech32m"], wallet="hot")
    segwit = rpc_call(port, "getnewaddress", ["", "bech32"], wallet="hot")
    rpc_call(port, "generatetoaddress", [101, taproot])
    rpc_call(port, "sendtoaddress", [segwit, 5], wallet="hot")
    rpc_call(port, "generatetoaddress", [1, taproot])
    return taproot, segwit


BASE58 = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"


def with_key_version(key, version):
    """Re-encode an extended key with another version, such as a tpub as a vpub."""
    number = 0
    for char in key:
        number = number * 58 + BASE58.index(char)
    payload = bytes.fromhex(version) + number.to_bytes(82, "big")[4:78]
    data = payload + hashlib.sha256(hashlib.sha256(payload).digest()).digest()[:4]
    number = int.from_bytes(data, "big")
    encoded = ""
    while number:
        number, rest = divmod(number, 58)
        encoded = BASE58[rest] + encoded
    return encoded


def create_view_only(gui, name, import_text, birth_choice=None):
    open_type_selector(gui)
    gui.click("walletTypeViewOnly")
    gui.wait_for_page("createWalletFormPage", timeout_ms=5000)
    gui.set_text("createWalletNameInput", name)
    gui.set_text("watchOnlyXpubInput", import_text)
    assert gui.get_property("watchOnlyAddressType", "visible") is False
    if birth_choice:
        gui.wait_for_property("watchOnlyBirthDate", "visible", True, timeout_ms=5000)
        gui.click(f"watchOnlyBirth_{birth_choice}")
    else:
        assert gui.get_property("watchOnlyBirthDate", "visible") is False
    gui.wait_for_property("createWalletFormCreateButton", "enabled", True, timeout_ms=5000)
    gui.click("createWalletFormCreateButton")
    gui.wait_for_page("walletCreationReadyPage", timeout_ms=30000)
    gui.click("createWalletReadyDoneButton")
    gui.wait_for_property("walletBadge", "loading", False, timeout_ms=15000)


def case_listdescriptors_json_restores_history(harness):
    """listdescriptors output keeps the creation times, so the rescan finds payments made before the import."""
    harness.start_gui()
    gui = harness.driver
    fund_hot_wallet(harness)
    listed = rpc_call(harness.gui_rpc_port, "listdescriptors", wallet="hot")

    create_view_only(gui, "watch_json", json.dumps(listed))
    port = harness.gui_rpc_port
    assert rpc_call(port, "getwalletinfo", wallet="watch_json")["private_keys_enabled"] is False
    original = rpc_call(port, "getbalances", wallet="hot")["mine"]
    watched = rpc_call(port, "getbalances", wallet="watch_json")["mine"]
    assert original["trusted"] > 0 and original["immature"] > 0, original
    assert original == watched, f"{original} != {watched}"
    # Descriptors carry no address book, so a payment to an own address lists as change.
    # Compare coins and known transactions instead of listed entries.
    def coins(wallet):
        return sorted((utxo["txid"], utxo["vout"], utxo["amount"]) for utxo in rpc_call(port, "listunspent", [0], wallet=wallet))
    assert coins("hot") == coins("watch_json"), (coins("hot"), coins("watch_json"))
    for tx in rpc_call(port, "listtransactions", ["*", 1000], wallet="hot"):
        rpc_call(port, "gettransaction", [tx["txid"]], wallet="watch_json")
    for method, params in (("getnewaddress", ["", "bech32m"]), ("getrawchangeaddress", ["bech32"])):
        assert rpc_call(port, method, params, wallet="hot") == rpc_call(port, method, params, wallet="watch_json"), method
    print("  listdescriptors JSON restored balance, history and next addresses")


def case_descriptor_lines_keep_their_address_type(harness):
    """Taproot descriptors pasted as lines watch Taproot addresses, scanned from the chosen date."""
    harness.start_gui()
    gui = harness.driver
    taproot, segwit = fund_hot_wallet(harness)
    listed = rpc_call(harness.gui_rpc_port, "listdescriptors", wallet="hot")["descriptors"]
    lines = [item["desc"] for item in listed if item["active"] and item["desc"].startswith("tr(")]
    assert len(lines) == 2, lines

    create_view_only(gui, "watch_taproot", "\n".join(lines), birth_choice="unknown")
    port = harness.gui_rpc_port
    assert rpc_call(port, "getaddressinfo", [taproot], wallet="watch_taproot")["ismine"] is True
    assert rpc_call(port, "getaddressinfo", [segwit], wallet="watch_taproot")["ismine"] is False
    received = [rpc_call(port, "getreceivedbyaddress", [taproot, 0, True], wallet=wallet) for wallet in ("hot", "watch_taproot")]
    assert received[0] > 0 and received[0] == received[1], received
    print("  Taproot lines watch Taproot addresses, history found")


def case_keys_keep_their_address_type(harness):
    """A Taproot account key with its origin, and a vpub, watch the addresses their wallet used."""
    harness.start_gui()
    gui = harness.driver
    taproot, segwit = fund_hot_wallet(harness)
    port = harness.gui_rpc_port
    listed = rpc_call(port, "listdescriptors", wallet="hot")["descriptors"]

    def account_key(script):
        desc = next(item["desc"] for item in listed if item["active"] and not item["internal"] and item["desc"].startswith(script))
        return re.search(r"(\[[0-9a-f]{8}[^\]]*\])(tpub[A-Za-z0-9]+)", desc).groups()

    origin, key = account_key("tr(")
    create_view_only(gui, "watch_taproot_key", origin + key, birth_choice="unknown")
    assert rpc_call(port, "getaddressinfo", [taproot], wallet="watch_taproot_key")["ismine"] is True
    received = [rpc_call(port, "getreceivedbyaddress", [taproot, 0, True], wallet=wallet) for wallet in ("hot", "watch_taproot_key")]
    assert received[0] > 0 and received[0] == received[1], received
    print("  Taproot account key with its origin watches Taproot addresses")

    vpub = with_key_version(account_key("wpkh(")[1], "045f1cf6")
    create_view_only(gui, "watch_vpub", vpub, birth_choice="unknown")
    assert rpc_call(port, "getaddressinfo", [segwit], wallet="watch_vpub")["ismine"] is True
    assert rpc_call(port, "getreceivedbyaddress", [segwit], wallet="watch_vpub") == 5
    print("  vpub watches SegWit addresses and finds the earlier payment")


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


def case_type_selector_import_flow(harness):
    """Test that import wallet option in type selector routes correctly."""
    harness.start_gui()
    gui = harness.driver

    open_type_selector(gui)
    print("  Type selector opened")

    invalid_backup = os.path.join(os.path.dirname(__file__), "fixtures", "invalid-wallet-backup.bak")
    gui.set_text("importWalletPathField", invalid_backup)
    gui.click("walletTypeImport")
    gui.wait_for_page("walletImportErrorPage", timeout_ms=15000)
    print("  Import error shown directly from type selector")

    print("  Import wallet type selector flow PASSED")


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
        "listdescriptors_json",
        port_offset=120,
        case_body=case_listdescriptors_json_restores_history,
    )

    failures += run_case(
        "descriptor_lines",
        port_offset=130,
        case_body=case_descriptor_lines_keep_their_address_type,
    )

    failures += run_case(
        "extended_keys",
        port_offset=140,
        case_body=case_keys_keep_their_address_type,
    )

    failures += run_case(
        "type_selector_regular",
        port_offset=90,
        case_body=case_type_selector_regular_flow,
    )

    failures += run_case(
        "type_selector_import",
        port_offset=100,
        case_body=case_type_selector_import_flow,
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
