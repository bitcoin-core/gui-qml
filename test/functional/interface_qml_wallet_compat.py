#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Regression test for bitcoin-qt loading QML-created wallet request metadata."""

import os
import re
import shlex
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path
from qml_driver import QmlDriverError
from qml_scenarios import (
    run_scenarios,
    scenario,
)
from qml_smoke_lib import (
    create_request,
    stop_gui_checked,
    wallet_scenario,
)
from qml_test_harness import dump_qml_tree
from qml_qt_release_lib import (
    compat_bitcoin_qt_or_skip,
    find_compat_bitcoin_qt,
    has_bitcoin_qt_runner,
    launch_bitcoin_qt,
    process_output,
    stop_bitcoin_qt,
    wait_for_rpc_or_process_exit,
)
from qml_wallet_test_lib import (
    WalletFlowHarness,
    find_legacy_bitcoind,
    open_wallet_selector,
    rpc_call,
)


COMPAT_VERSION = "31.0"
WALLET_NAME = "qt_compat_wallet"
REQUEST_AMOUNT = "0.001"
REQUEST_LABEL = "QML Qt Compat"
REQUEST_MESSAGE = "Compatibility request from bitcoin-core-app"
REQUEST_NOTE_SELF = "Private compatibility note"


def debug_log_tail(datadir, lines=200):
    debug_log = os.path.join(datadir, "regtest", "debug.log")
    if not os.path.isfile(debug_log):
        return ""
    with open(debug_log, "r", encoding="utf8", errors="replace") as log_file:
        return "".join(log_file.readlines()[-lines:])


def create_unencrypted_wallet(gui, wallet_name):
    try:
        gui.wait_for_property("createWalletButton", "visible", True, timeout_ms=1000)
        gui.wait_for_property("createWalletButton", "enabled", True, timeout_ms=25000)
        gui.click("createWalletButton")
    except QmlDriverError:
        pass
    gui.wait_for_property("walletTypeRegular", "visible", True, timeout_ms=5000)
    gui.click("walletTypeRegular")
    gui.wait_for_page("createWalletFormPage", timeout_ms=5000)
    gui.set_text("createWalletNameInput", wallet_name)
    gui.click("createWalletEncryptCheckBox")
    gui.click("createWalletUnencryptedConfirmToggle")
    gui.wait_for_property("createWalletFormCreateButton", "enabled", True, timeout_ms=10000)
    gui.click("createWalletFormCreateButton")
    gui.wait_for_property("createWalletUnencryptedWarning", "opened", True, timeout_ms=5000)
    gui.click("createWalletWarningConfirmButton")
    gui.wait_for_page("walletCreationReadyPage", timeout_ms=20000)
    gui.click("createWalletReadyDoneButton")
    gui.wait_for_property("walletBadge", "text", wallet_name, timeout_ms=20000)


def create_full_payment_request(gui):
    gui.click("receiveTabButton")
    gui.wait_for_page("receiveWorkspace", timeout_ms=10000)
    gui.wait_for_property("receiveAmountInput", "visible", True, timeout_ms=10000)
    create_request(gui, REQUEST_AMOUNT, REQUEST_LABEL, REQUEST_MESSAGE, REQUEST_NOTE_SELF)
    gui.wait_for_property("paymentRequestModal", "opened", True, timeout_ms=10000)
    gui.wait_for_property("paymentRequestModal", "openedEditor.busy", False, timeout_ms=10000)
    request_id = gui.get_property("paymentRequestModal", "openedEditor.requestId")
    address = gui.get_property("paymentRequestModal", "openedEditor.address")
    assert request_id and address, "The compatibility check requires the original saved request"
    assert gui.get_property("paymentRequestModal", "wallet.name") == WALLET_NAME
    assert gui.get_property("paymentRequestModal", "openedEditor.dirty") is False
    assert gui.get_property("paymentRequestModal", "openedEditor.amount.satoshi") == 100_000
    assert gui.get_property("paymentRequestModal", "openedEditor.publicLabel.text") == REQUEST_LABEL
    assert gui.get_property("paymentRequestModal", "openedEditor.message.text") == REQUEST_MESSAGE
    assert gui.get_property("paymentRequestModal", "openedEditor.privateNote.text") == REQUEST_NOTE_SELF
    assert gui.get_property("paymentRequestModal", "openedEditor.requestId") == request_id
    assert gui.get_property("paymentRequestModal", "openedEditor.address") == address


def qt_metadata_compatibility():
    bitcoin_qt = find_compat_bitcoin_qt()
    if not bitcoin_qt:
        print(f"SKIPPED: bitcoin-qt {COMPAT_VERSION} not found.")
        print(
            f"Set BITCOIN_QT_COMPAT or provide releases/v{COMPAT_VERSION}/bin/bitcoin-qt "
            "to exercise this flow."
        )
        return 77
    if not has_bitcoin_qt_runner():
        print(bitcoin_qt_runner_skip_message())
        return 77

    harness = WalletFlowHarness("qml_wallet_qt_compat", port_offset=95)
    qt_process = None
    try:
        print(f"[qml_wallet_qt_compat] bitcoin-qt located: {bitcoin_qt}")
        harness.start_gui()
        gui = harness.driver
        print("[qml_wallet_qt_compat] QML GUI launched")
        harness.finish_onboarding()

        create_unencrypted_wallet(gui, WALLET_NAME)
        print("[qml_wallet_qt_compat] wallet created in bitcoin-core-app")

        create_full_payment_request(gui)
        print("[qml_wallet_qt_compat] payment request with all visible fields created")

        stop_gui_checked(harness)
        print("[qml_wallet_qt_compat] bitcoin-core-app stopped")

        qt_process = launch_bitcoin_qt(bitcoin_qt, harness)
        wait_for_rpc_or_process_exit(qt_process, harness.gui_rpc_port, harness.tmpdir)
        loaded_wallets = rpc_call(harness.gui_rpc_port, "listwallets")
        assert WALLET_NAME in loaded_wallets, f"Expected {WALLET_NAME} to load, got {loaded_wallets}"
        wallet_info = rpc_call(harness.gui_rpc_port, "getwalletinfo", wallet=WALLET_NAME)
        assert wallet_info["walletname"] == WALLET_NAME, f"Unexpected loaded wallet: {wallet_info}"

        time.sleep(3)
        if qt_process.poll() is not None:
            raise AssertionError(
                f"bitcoin-qt exited after loading wallet with code {qt_process.returncode}.\n"
                f"{process_output(qt_process, harness.tmpdir)}"
            )

        print("[qml_wallet_qt_compat] bitcoin-qt stayed alive after loading QML-created request metadata")
        stop_bitcoin_qt(qt_process, harness.gui_rpc_port)
        print(f"[qml_wallet_qt_compat] bitcoin-qt exit={qt_process.returncode}")
        assert qt_process.returncode == 0, "Qt Widgets must exit cleanly after the metadata compatibility check"
        return 0
    except Exception as err:  # noqa: BLE001 - preserve failure context
        print(f"\nFAILED [qml_wallet_qt_compat]: {err}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        if harness.driver is not None:
            dump_qml_tree(harness.driver)
        qt_output = process_output(qt_process, harness.tmpdir)
        if qt_output:
            print("\n--- bitcoin-qt process output ---", file=sys.stderr)
            print(qt_output, file=sys.stderr)
        log_tail = debug_log_tail(harness.gui_datadir)
        if log_tail:
            print("\n--- debug.log tail ---", file=sys.stderr)
            print(log_tail, file=sys.stderr)
        return 1
    finally:
        stop_bitcoin_qt(qt_process, harness.gui_rpc_port)
        harness.stop()


WALLET_PASSWORD = "correct horse battery staple"
LEGACY_MIGRATION_WARNING = "This wallet is a legacy wallet and will need to be migrated with migratewallet before it can be loaded"


def sanitize_object_suffix(value):
    return re.sub(r"[^A-Za-z0-9_]+", "_", value)


def verify_legacy_wallet_warning(gui_rpc_port, wallet_name):
    wallet_dir = rpc_call(gui_rpc_port, "listwalletdir")["wallets"]
    legacy_entry = next((entry for entry in wallet_dir if entry["name"] == wallet_name), None)
    assert legacy_entry is not None, f"Expected {wallet_name} to be present in listwalletdir"
    assert legacy_entry["warnings"] == [
        LEGACY_MIGRATION_WARNING
    ], f"Expected migration warning for {wallet_name}, got {legacy_entry}"
    print(f"[interface_qml_wallet_compat] legacy wallet dir entry: {legacy_entry}")


def select_wallet_for_migration(gui, wallet_name):
    open_wallet_selector(gui)
    gui.wait_for_object(f"walletSelectItem_{sanitize_object_suffix(wallet_name)}", timeout_ms=5000)
    gui.click(f"walletSelectItem_{sanitize_object_suffix(wallet_name)}")
    gui.wait_for_property("walletMigrationPopup", "opened", True, timeout_ms=10000)


def verify_migrated_wallet(harness, wallet_name, target_wallet_path, *, encrypted=False):
    wallet_dat_path = os.path.join(target_wallet_path, "wallet.dat")
    with open(wallet_dat_path, "rb") as wallet_file:
        assert wallet_file.read(16) == b"SQLite format 3\x00", "Migrated wallet should be SQLite"

    wallet_info = rpc_call(harness.gui_rpc_port, "getwalletinfo", wallet=wallet_name)
    assert wallet_info["descriptors"] is True, "Migrated wallet should be descriptor based"
    assert wallet_info["format"] == "sqlite", "Migrated wallet should be stored as sqlite"
    if encrypted:
        assert wallet_info["unlocked_until"] == 0, (
            f"Expected migrated encrypted wallet to be locked, got {wallet_info}"
        )
    print(f"[interface_qml_wallet_compat] migrated wallet info: {wallet_info}")


def wait_popup_closed(gui, name, timeout_ms=5000):
    # Immutable prompt delegates may be destroyed after their original closes.
    # Retain the existing watchdog and accept either genuine teardown form.
    deadline = time.monotonic() + timeout_ms / 1000
    while time.monotonic() < deadline:
        if not gui.object_exists(name) or not gui.get_property(name, "visible"):
            return
        time.sleep(0.05)
    raise AssertionError(f"Original popup {name} did not close")


def legacy_restore_and_migration(harness, checkpoint):
    legacy = find_legacy_bitcoind()
    if legacy is None:
        print("SKIPPED: BITCOIND_LEGACY or the official v28 daemon is required")
        raise SystemExit(77)
    paths, backups, addresses = {}, {}, {}
    harness.start_source_node(binary=legacy, extra_args=["-deprecatedrpc=create_bdb"])
    for name, encrypted in (("legacy_flow", False), ("legacy_encrypted_flow", True)):
        rpc_call(harness.source_rpc_port, "createwallet", {"wallet_name": name, "descriptors": False})
        addresses[name] = rpc_call(harness.source_rpc_port, "getnewaddress", [], wallet=name)
        if encrypted:
            rpc_call(harness.source_rpc_port, "encryptwallet", [WALLET_PASSWORD], wallet=name)
        backup = Path(harness.tmpdir) / ("legacy_encrypted_import.bak" if encrypted else "legacy_import.bak")
        rpc_call(harness.source_rpc_port, "backupwallet", [str(backup)], wallet=name)
        backups[name] = (backup, backup.read_bytes())
        rpc_call(harness.source_rpc_port, "unloadwallet", [name])
    harness.stop_source_node()
    for name in ("legacy_flow", "legacy_encrypted_flow"):
        target = Path(harness.gui_wallets_path) / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(Path(harness.source_wallets_path) / name, target)
        paths[name] = str(target)
    checkpoint("actual plain/encrypted v28 wallets and unchanged backup fixtures created")
    harness.start_gui()
    gui = harness.driver
    gui.wait_for_property("walletBadge", "loading", False, timeout_ms=30000)
    for name, encrypted in (("legacy_flow", False), ("legacy_encrypted_flow", True)):
        backup, original = backups[name]
        harness.open_create_wallet_wizard()
        gui.set_text("importWalletPathField", str(backup))
        gui.click("walletTypeImport")
        gui.wait_for_property("walletMigrationPopup", "opened", True, timeout_ms=30000)
        assert backup.read_bytes() == original
        assert not (Path(harness.gui_wallets_path) / backup.stem).exists(), "Confirmation must precede copying/migration"
        gui.click("walletMigrationConfirmButton")
        if encrypted:
            gui.wait_for_property("walletMigrationPassphrasePopup", "opened", True)
            gui.set_text("walletMigrationPassphraseField", WALLET_PASSWORD)
            gui.wait_for_property("walletMigrationPassphraseConfirmButton", "enabled", True)
            gui.click("walletMigrationPassphraseConfirmButton")
        gui.wait_for_page("importWalletSuccessPage", timeout_ms=30000)
        assert gui.get_property("walletBadge", "text") != backup.stem, "Restore completion must not select the result"
        verify_migrated_wallet(harness, backup.stem, str(Path(harness.gui_wallets_path) / backup.stem), encrypted=encrypted)
        assert rpc_call(harness.gui_rpc_port, "getaddressinfo", [addresses[name]], wallet=backup.stem)["ismine"]
        assert backup.read_bytes() == original, "Migration must preserve the selected backup bytes"
        gui.click("importWalletSuccessOverviewButton")
        gui.wait_for_property("walletBadge", "text", backup.stem)
        # The previous Loader can still exist during its real exit transition.
        # Wait for that original modal to close before opening a fresh wizard.
        wait_popup_closed(gui, "walletCreationModal")
        rpc_call(harness.gui_rpc_port, "unloadwallet", [backup.stem, False])
        checkpoint(f"{name} backup restored/migrated with explicit original-result selection")
    stop_gui_checked(harness)
    harness.start_gui()
    gui = harness.driver
    gui.wait_for_property("walletBadge", "loading", False, timeout_ms=30000)
    for name in paths:
        verify_legacy_wallet_warning(harness.gui_rpc_port, name)
    select_wallet_for_migration(gui, "legacy_flow")
    gui.click("walletMigrationCancelButton")
    wait_popup_closed(gui, "walletMigrationPopup")
    assert rpc_call(harness.gui_rpc_port, "listwallets") == []
    select_wallet_for_migration(gui, "legacy_flow")
    gui.click("walletMigrationConfirmButton")
    gui.wait_for_property("walletMigrationResultPopup", "opened", True, timeout_ms=30000)
    assert gui.get_property("walletBadge", "text") != "legacy_flow"
    verify_migrated_wallet(harness, "legacy_flow", paths["legacy_flow"])
    assert rpc_call(harness.gui_rpc_port, "getaddressinfo", [addresses["legacy_flow"]], wallet="legacy_flow")["ismine"]
    assert list(Path(harness.gui_wallets_path).glob("legacy_flow_*.legacy.bak")), "Core recovery backup must remain"
    gui.click("walletMigrationOpenResultButton")
    gui.wait_for_property("walletBadge", "text", "legacy_flow")
    wait_popup_closed(gui, "walletMigrationResultPopup")
    checkpoint("unencrypted legacy wallet migrated; original recovery and keys retained")
    select_wallet_for_migration(gui, "legacy_encrypted_flow")
    gui.click("walletMigrationConfirmButton")
    gui.wait_for_property("walletMigrationPassphrasePopup", "opened", True)
    gui.settle()
    gui.wait_for_property("walletMigrationPassphraseErrorText", "text", "")
    gui.set_text("walletMigrationPassphraseField", "wrong password")
    gui.wait_for_property("walletMigrationPassphraseConfirmButton", "enabled", True)
    gui.click("walletMigrationPassphraseConfirmButton")
    # Preserve the original 10s password-derivation watchdog and wipe oracle.
    gui.wait_for_property("walletMigrationPassphraseErrorText", "text", lambda text: "passphrase" in text.lower(), timeout_ms=10000)
    gui.wait_for_property("walletMigrationPassphraseField", "text", "")
    gui.set_text("walletMigrationPassphraseField", WALLET_PASSWORD)
    gui.wait_for_property("walletMigrationPassphraseConfirmButton", "enabled", True)
    gui.click("walletMigrationPassphraseConfirmButton")
    gui.wait_for_property("walletMigrationResultPopup", "opened", True, timeout_ms=30000)
    assert gui.get_property("walletBadge", "text") == "legacy_flow", "A later result must not replace the selected wallet"
    verify_migrated_wallet(harness, "legacy_encrypted_flow", paths["legacy_encrypted_flow"], encrypted=True)
    assert rpc_call(harness.gui_rpc_port, "getaddressinfo", [addresses["legacy_encrypted_flow"]], wallet="legacy_encrypted_flow")["ismine"]
    assert list(Path(harness.gui_wallets_path).glob("legacy_encrypted_flow_*.legacy.bak"))
    gui.click("walletMigrationOpenResultButton")
    gui.wait_for_property("walletBadge", "text", "legacy_encrypted_flow")
    checkpoint("encrypted legacy wallet migrated/relocked through a fresh exact prompt")
    # Representative older-GUI compatibility uses the actual migrated files,
    # original public addresses and encryption state, not a mock format check.
    bitcoin_qt = find_compat_bitcoin_qt()
    if bitcoin_qt is None or not has_bitcoin_qt_runner():
        raise RuntimeError("Actual Qt Widgets release and runner are required for the R13 migration compatibility control")
    stop_gui_checked(harness)
    qt_process = launch_bitcoin_qt(bitcoin_qt, harness.gui_datadir, harness.tmpdir)
    try:
        wait_for_rpc_or_process_exit(qt_process, harness.gui_rpc_port, harness.tmpdir)
        for name, encrypted in (("legacy_flow", False), ("legacy_encrypted_flow", True)):
            verify_migrated_wallet(harness, name, paths[name], encrypted=encrypted)
            assert rpc_call(harness.gui_rpc_port, "getaddressinfo", [addresses[name]], wallet=name)["ismine"]
        assert qt_process.poll() is None
    finally:
        stop_bitcoin_qt(qt_process, harness.gui_rpc_port)
        output = process_output(qt_process, harness.tmpdir)
        print(f"[R13 migrated-wallet Widgets compatibility] exit={qt_process.returncode}\n{output}")
    assert qt_process.returncode == 0, "Qt Widgets must exit cleanly after opening migrated wallets"
    checkpoint("actual Qt Widgets release opened both migrated wallets with original keys and encryption state")


if __name__ == "__main__":
    raise SystemExit(run_scenarios('Legacy migration and Qt release compatibility', [
        wallet_scenario("legacy-restore-and-migration", 4, legacy_restore_and_migration),
        scenario("qt-request-metadata", 5, qt_metadata_compatibility),
    ]))
