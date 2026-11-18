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


def find_compat_bitcoin_qt():
    explicit = os.getenv("BITCOIN_QT_COMPAT")
    if explicit and os.path.isfile(explicit):
        return explicit

    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
    releases_root = os.getenv("PREVIOUS_RELEASES_DIR", os.path.join(repo_root, "releases"))
    candidate = os.path.join(releases_root, f"v{COMPAT_VERSION}", "bin", "bitcoin-qt")
    if os.path.isfile(candidate):
        return candidate

    return None


def process_output(process):
    if not process or process.poll() is None:
        return ""
    stdout = process.stdout.read().decode("utf-8", errors="replace") if process.stdout else ""
    stderr = process.stderr.read().decode("utf-8", errors="replace") if process.stderr else ""
    if stdout and stderr:
        return f"stdout:\n{stdout}\n\nstderr:\n{stderr}"
    return stdout or stderr


def debug_log_tail(datadir, lines=200):
    debug_log = os.path.join(datadir, "regtest", "debug.log")
    if not os.path.isfile(debug_log):
        return ""
    with open(debug_log, "r", encoding="utf8", errors="replace") as log_file:
        return "".join(log_file.readlines()[-lines:])


def wait_for_rpc_or_process_exit(process, port, timeout=45):
    deadline = time.time() + timeout
    last_error = None
    while time.time() < deadline:
        if process.poll() is not None:
            raise AssertionError(
                f"bitcoin-qt exited before RPC became ready with code {process.returncode}.\n"
                f"{process_output(process)}"
            )
        try:
            rpc_call(port, "getblockcount")
            return
        except Exception as err:  # noqa: BLE001 - startup should tolerate transient RPC errors
            last_error = err
            time.sleep(0.25)
    raise AssertionError(f"bitcoin-qt RPC did not become ready before timeout: {last_error}")


def stop_bitcoin_qt(process, rpc_port):
    if not process:
        return
    if process.poll() is not None:
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


def bitcoin_qt_command(bitcoin_qt):
    runner = os.getenv("BITCOIN_QT_RUNNER")
    if runner:
        return shlex.split(runner) + [bitcoin_qt]
    xvfb_run = shutil.which("xvfb-run")
    if xvfb_run:
        return [xvfb_run, "-a", bitcoin_qt]
    if os.getenv("BITCOIN_QT_USE_DISPLAY") == "1" and os.getenv("DISPLAY"):
        return [bitcoin_qt]
    return None


def has_bitcoin_qt_runner():
    return bitcoin_qt_command("/bin/true") is not None


def bitcoin_qt_runner_skip_message():
    return (
        "SKIPPED: bitcoin-qt compatibility test needs DISPLAY, xvfb-run, "
        "or BITCOIN_QT_RUNNER. Set BITCOIN_QT_USE_DISPLAY=1 to use the "
        "ambient DISPLAY directly."
    )


def require_bitcoin_qt_command(bitcoin_qt):
    command = bitcoin_qt_command(bitcoin_qt)
    if command is None:
        raise RuntimeError(bitcoin_qt_runner_skip_message())
    return command


def launch_bitcoin_qt(bitcoin_qt, harness):
    env = dict(os.environ)
    env.setdefault("XDG_CONFIG_HOME", os.path.join(harness.tmpdir, "qt_compat_config"))
    os.makedirs(env["XDG_CONFIG_HOME"], exist_ok=True)
    command = require_bitcoin_qt_command(bitcoin_qt) + [
        f"-datadir={harness.gui_datadir}",
        "-nosplash",
        "-min",
        "-debug",
        "-debugexclude=leveldb",
        "-printtoconsole=1",
    ]
    print(f"[qml_wallet_qt_compat] starting bitcoin-qt: {' '.join(command)}")
    return subprocess.Popen(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


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
    gui.wait_for_page("requestPaymentPage", timeout_ms=10000)
    create_request(gui, REQUEST_AMOUNT, REQUEST_LABEL, REQUEST_MESSAGE, REQUEST_NOTE_SELF)
    gui.wait_for_property("requestPaymentTitle", "text", "Payment request", timeout_ms=10000)


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
        wait_for_rpc_or_process_exit(qt_process, harness.gui_rpc_port)
        loaded_wallets = rpc_call(harness.gui_rpc_port, "listwallets")
        assert WALLET_NAME in loaded_wallets, f"Expected {WALLET_NAME} to load, got {loaded_wallets}"
        wallet_info = rpc_call(harness.gui_rpc_port, "getwalletinfo", wallet=WALLET_NAME)
        assert wallet_info["walletname"] == WALLET_NAME, f"Unexpected loaded wallet: {wallet_info}"

        time.sleep(3)
        if qt_process.poll() is not None:
            raise AssertionError(
                f"bitcoin-qt exited after loading wallet with code {qt_process.returncode}.\n"
                f"{process_output(qt_process)}"
            )

        print("[qml_wallet_qt_compat] bitcoin-qt stayed alive after loading QML-created request metadata")
        return 0
    except Exception as err:  # noqa: BLE001 - preserve failure context
        print(f"\nFAILED [qml_wallet_qt_compat]: {err}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        if harness.driver is not None:
            dump_qml_tree(harness.driver)
        qt_output = process_output(qt_process)
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


def legacy_restore_and_migration(harness, checkpoint):
    legacy = find_legacy_bitcoind()
    if legacy is None:
        print("SKIPPED: BITCOIND_LEGACY or the official v28 daemon is required")
        raise SystemExit(77)
    paths = {}
    # Both wallets are produced by one legacy process, then its files are copied
    # only after shutdown releases their database handles.
    harness.start_source_node(binary=legacy, extra_args=["-deprecatedrpc=create_bdb"])
    backup = Path(harness.tmpdir) / "legacy_import.bak"
    for name, encrypted in (("legacy_flow", False), ("legacy_encrypted_flow", True)):
        rpc_call(harness.source_rpc_port, "createwallet", {"wallet_name": name, "descriptors": False})
        if encrypted:
            rpc_call(harness.source_rpc_port, "encryptwallet", [WALLET_PASSWORD], wallet=name)
        if not encrypted:
            rpc_call(harness.source_rpc_port, "backupwallet", [str(backup)], wallet=name)
        rpc_call(harness.source_rpc_port, "unloadwallet", [name])
    harness.stop_source_node()
    for name in ("legacy_flow", "legacy_encrypted_flow"):
        target = Path(harness.gui_wallets_path) / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(Path(harness.source_wallets_path) / name, target)
        paths[name] = str(target)
    checkpoint("legacy wallet fixtures and backup created")
    harness.start_gui()
    gui = harness.driver
    gui.wait_for_property("walletBadge", "loading", False, timeout_ms=30000)
    harness.open_create_wallet_wizard()
    gui.set_text("importWalletPathField", str(backup))
    gui.click("walletTypeImport")
    gui.wait_for_page("walletImportErrorPage", timeout_ms=30000)
    gui.wait_for_property("importWalletErrorTitle", "text", "This wallet needs to be migrated")
    assert not (Path(harness.gui_wallets_path) / "legacy_import").exists()
    assert rpc_call(harness.gui_rpc_port, "listwallets") == []
    checkpoint("legacy backup requires migration without leaving a broken wallet")
    # A fresh GUI process leaves the import wizard and discovers the two closed
    # legacy wallets through the same startup path used by an actual user.
    stop_gui_checked(harness)
    harness.start_gui()
    gui = harness.driver
    gui.wait_for_property("walletBadge", "loading", False, timeout_ms=30000)
    for name in paths:
        verify_legacy_wallet_warning(harness.gui_rpc_port, name)
    select_wallet_for_migration(gui, "legacy_flow")
    gui.click("walletMigrationCancelButton")
    gui.wait_for_property("walletMigrationPopup", "visible", False)
    assert rpc_call(harness.gui_rpc_port, "listwallets") == []
    select_wallet_for_migration(gui, "legacy_flow")
    gui.click("walletMigrationConfirmButton")
    gui.wait_for_property("walletBadge", "text", "legacy_flow", timeout_ms=30000)
    gui.wait_for_property("walletMigrationPopup", "visible", False)
    verify_migrated_wallet(harness, "legacy_flow", paths["legacy_flow"])
    checkpoint("unencrypted legacy wallet migrated")
    select_wallet_for_migration(gui, "legacy_encrypted_flow")
    gui.click("walletMigrationConfirmButton")
    gui.wait_for_property("walletMigrationPassphrasePopup", "opened", True)
    gui.wait_for_property("walletMigrationPopup", "visible", False)
    gui.settle()
    gui.wait_for_property("walletMigrationPassphraseErrorText", "text", "")
    gui.set_text("walletMigrationPassphraseField", "wrong password")
    gui.wait_for_property("walletMigrationPassphraseConfirmButton", "enabled", True)
    gui.click("walletMigrationPassphraseConfirmButton")
    gui.wait_for_property("walletMigrationPassphraseField", "text", "")
    # Keep the original migration budget. The old daemon calibrates password
    # derivation on its own CPU; the instrumented GUI can exceed the default 5s.
    gui.wait_for_property("walletMigrationPassphraseErrorText", "text", lambda text: "passphrase" in text.lower(), timeout_ms=10000)
    gui.set_text("walletMigrationPassphraseField", WALLET_PASSWORD)
    gui.wait_for_property("walletMigrationPassphraseConfirmButton", "enabled", True)
    gui.click("walletMigrationPassphraseConfirmButton")
    gui.wait_for_property("walletBadge", "text", "legacy_encrypted_flow", timeout_ms=30000)
    gui.wait_for_property("walletMigrationPassphrasePopup", "visible", False)
    verify_migrated_wallet(harness, "legacy_encrypted_flow", paths["legacy_encrypted_flow"], encrypted=True)
    checkpoint("encrypted legacy wallet migrated and relocked")


if __name__ == "__main__":
    raise SystemExit(run_scenarios('Legacy migration and Qt release compatibility', [
        wallet_scenario("legacy-restore-and-migration", 4, legacy_restore_and_migration),
        scenario("qt-request-metadata", 5, qt_metadata_compatibility),
    ]))
