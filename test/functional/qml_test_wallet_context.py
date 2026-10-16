#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Wallet-context backup regression using real wallets and RPC load/unload.

The file picker is opened normally. Its late accepted callback is delivered
through the test bridge; test_qmlfiledialog.cpp separately checks that a stale
native opening cannot deliver acceptance after the chooser has reopened.
"""
from pathlib import Path

from qml_test_wallet_settings import open_wallet_settings, select_wallet
from qml_wallet_test_lib import WalletFlowHarness, rpc_call, wait_for_rpc


def run_tests():
    harness = WalletFlowHarness("qml_test_wallet_context", port_offset=570)
    try:
        harness.start_gui()
        gui = harness.driver
        port = harness.gui_rpc_port
        wait_for_rpc(port)
        gui.wait_for_property("walletBadge", "loading", False, timeout_ms=30000)
        for name in ("context_A", "context_B"):
            rpc_call(port, "createwallet", {"wallet_name": name, "load_on_startup": False})
            gui.wait_for_property("walletBadge", "text", name, timeout_ms=30000)
        addresses = {name: rpc_call(port, "getnewaddress", wallet=name)
                     for name in ("context_A", "context_B")}
        rpc_call(port, "unloadwallet", ["context_B", False])
        gui.wait_for_property("walletBadge", "text", "context_A", timeout_ms=10000)
        open_wallet_settings(gui)

        backup = Path(harness.tmpdir) / "existing-A.bak"
        rpc_call(port, "backupwallet", [str(backup)], wallet="context_A")
        original = backup.read_bytes()

        def start_backup():
            gui.invoke("walletSettingsPage", "startBackup")
            gui.wait_for_property("walletBackupDialog", "visible", True, timeout_ms=10000)

        def accept_callback(path):
            gui.set_property("walletBackupDialog", "selectedFile", path.as_uri())
            gui.invoke("walletBackupDialog", "accepted")

        # The original report: loading B while A's replacement picker is open.
        start_backup()
        rpc_call(port, "loadwallet", ["context_B", False])
        gui.wait_for_property("walletBadge", "text", "context_B", timeout_ms=10000)
        gui.wait_for_property("walletBackupDialog", "visible", False, timeout_ms=10000)
        accept_callback(backup)
        assert backup.read_bytes() == original, "A's existing backup was modified"
        print("PASS: RPC load cancels backup; existing backup unchanged")

        # Returning to A must not revive the canceled opening.
        select_wallet(gui, "context_A")
        gui.wait_for_property("walletBadge", "text", "context_A", timeout_ms=10000)
        accept_callback(backup)
        assert backup.read_bytes() == original, "Returning to A revived a canceled backup"
        print("PASS: A -> B -> A does not revive acceptance")

        # Unload/reload under the same name is a new wallet session.
        start_backup()
        rpc_call(port, "unloadwallet", ["context_A", False])
        gui.wait_for_property("walletBackupDialog", "visible", False, timeout_ms=10000)
        rpc_call(port, "loadwallet", ["context_A", False])
        gui.wait_for_property("walletBadge", "text", "context_A", timeout_ms=10000)
        accept_callback(backup)
        assert backup.read_bytes() == original, "Reloading A revived a canceled backup"
        print("PASS: unloading/reloading A leaves the existing backup unchanged")

        # A fresh operation must succeed and preserve the exact approved path.
        fresh = Path(harness.tmpdir) / "fresh-A.custom"
        start_backup()
        accept_callback(fresh)
        gui.invoke("walletBackupDialog", "close")
        assert fresh.is_file(), "A fresh backup was not written to the approved path"
        assert not Path(str(fresh) + ".bak").exists(), "The approved path was changed"
        rpc_call(port, "restorewallet", {
            "wallet_name": "context_restored", "backup_file": str(fresh),
            "load_on_startup": False,
        })
        for name, address in addresses.items():
            info = rpc_call(port, "getaddressinfo", [address], wallet="context_restored")
            assert info["ismine"] == (name == "context_A"), f"Backup contains {name} unexpectedly"
        wallet_info = rpc_call(port, "getwalletinfo", wallet="context_restored")
        assert wallet_info["private_keys_enabled"], "Backup lost its private-key capability"
        descriptors = rpc_call(port, "listdescriptors", [True], wallet="context_restored")
        assert descriptors["descriptors"], "Restored backup has no descriptors"
        assert any("prv" in row["desc"] for row in descriptors["descriptors"]), "Private descriptors missing"
        print("PASS: fresh backup restores A's private descriptors and addresses, not B's")
    finally:
        harness.stop()


if __name__ == "__main__":
    run_tests()
