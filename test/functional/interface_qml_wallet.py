#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Wallet creation, encrypted backup, close/reload and restoration."""

import shutil
from pathlib import Path
from qml_scenarios import run_scenarios
from qml_smoke_lib import (
    close_wallet,
    create_wallet,
    import_wallet,
    open_wallet_settings,
    select_wallet,
    stop_gui_checked,
    wallet_scenario,
)
from qml_wallet_test_lib import (
    open_wallet_selector,
    rpc_call,
)


def wallet_smoke(harness, checkpoint):
    """Exercise wallet management happy paths, including a real process restart."""
    name = "smoke_wallet"
    password = "correct horse battery staple"
    harness.start_gui()
    create_wallet(harness, name, password)
    address = rpc_call(harness.gui_rpc_port, "getnewaddress", ["Smoke signing address", "legacy"], wallet=name)
    checkpoint("encrypted wallet created")

    directory = Path(harness.tmpdir) / "backups"
    directory.mkdir()
    open_wallet_settings(harness.driver)
    gui = harness.driver
    gui.set_text("walletSettingsBackupPathField", str(directory))
    gui.click("walletBackupRow")
    gui.wait_for_property("walletBackupRow", "supportingText", "Backup saved.", timeout_ms=30000)
    backup = directory / f"{name}.bak"
    gui.wait_for_property("walletSettingsPage", "errorText", "")
    assert backup.is_file() and backup.stat().st_size > 0
    checkpoint("wallet backup created")

    close_wallet(harness, name)
    stop_gui_checked(harness)
    harness.start_gui()
    gui = harness.driver
    gui.wait_for_property("walletBadge", "loading", False, timeout_ms=30000)
    select_wallet(gui, name)
    checkpoint("closed wallet loaded and explicitly selected after restart")

    close_wallet(harness, name)
    restored = Path(harness.tmpdir) / "restored_smoke.bak"
    shutil.copyfile(backup, restored)
    import_wallet(harness, restored)
    assert rpc_call(harness.gui_rpc_port, "getaddressinfo", [address], wallet=restored.stem)["ismine"]
    checkpoint("backup restored through the wallet wizard")

def backup_recovery_disclosure(harness, checkpoint):
    """Set a password in the GUI, then restore separate old/fresh copies."""
    harness.start_gui()
    gui = harness.driver
    name = "recovery_live"
    gui.wait_for_property("walletBadge", "loading", False, timeout_ms=30000)
    rpc_call(harness.gui_rpc_port, "createwallet", {"wallet_name": name})
    select_wallet(gui, name)
    original_address = rpc_call(harness.gui_rpc_port, "getnewaddress", wallet=name)
    old_backup = Path(harness.tmpdir) / "before-password.bak"
    fresh_backup = Path(harness.tmpdir) / "after-password.bak"
    open_wallet_settings(gui)
    gui.set_text("walletSettingsBackupPathField", str(old_backup))
    gui.click("walletBackupRow")
    gui.wait_for_property("walletBackupRow", "supportingText", "Backup saved.", timeout_ms=30000)
    assert old_backup.is_file()
    original_bytes = old_backup.read_bytes()
    gui.click("walletPasswordRow")
    gui.wait_for_page("walletPasswordSettingsPage")
    intro = gui.get_property("walletPasswordIntroTextDescription", "text")
    assert "receiving and change keys" in intro and "earlier backups" in intro
    gui.set_text("walletPasswordNewField", "recovery password")
    gui.set_text("walletPasswordConfirmField", "recovery password")
    gui.click("walletPasswordConfirmToggle")
    gui.wait_for_property("walletPasswordSaveButton", "enabled", True)
    gui.click("walletPasswordSaveButton")
    gui.wait_for_page("walletSettingsPage")
    gui.wait_for_property("walletPasswordRow", "title", "Update password")
    text = gui.get_property("walletBackupRow", "description")
    assert "earlier backups" in text and "Keep your older backups" in text
    assert rpc_call(harness.gui_rpc_port, "getwalletinfo", wallet=name)["unlocked_until"] == 0
    later_address = rpc_call(harness.gui_rpc_port, "getnewaddress", wallet=name)
    gui.set_text("walletSettingsBackupPathField", str(fresh_backup))
    gui.click("walletBackupRow")
    gui.wait_for_property("walletBackupRow", "supportingText", "Backup saved.", timeout_ms=30000)
    assert fresh_backup.is_file() and fresh_backup.stat().st_size > 0
    checkpoint("password disclosure retained; fresh-backup action wrote a real encrypted backup")
    for restored, backup in (("recovery_old", old_backup), ("recovery_fresh", fresh_backup)):
        rpc_call(harness.gui_rpc_port, "restorewallet", [restored, str(backup)])
        assert rpc_call(harness.gui_rpc_port, "getaddressinfo", [original_address], wallet=restored)["ismine"]
    assert not rpc_call(harness.gui_rpc_port, "getaddressinfo", [later_address], wallet="recovery_old")["ismine"]
    assert rpc_call(harness.gui_rpc_port, "getaddressinfo", [later_address], wallet="recovery_fresh")["ismine"]
    assert rpc_call(harness.gui_rpc_port, "getaddressinfo", [later_address], wallet=name)["ismine"]
    assert old_backup.read_bytes() == original_bytes
    assert name in rpc_call(harness.gui_rpc_port, "listwallets")
    open_wallet_selector(gui)
    gui.click(f"walletSelectItem_{name}")
    gui.wait_for_property("walletBadge", "text", name, timeout_ms=30000)
    checkpoint("separate old/fresh restores verify coverage; original live wallet and old backup preserved")


def backup_final_destination(harness, checkpoint):
    """The backend enforces final-path consent and preserves both sentinels."""
    harness.start_gui()
    gui = harness.driver
    gui.wait_for_property("walletBadge", "loading", False, timeout_ms=30000)
    rpc_call(harness.gui_rpc_port, "createwallet", {"wallet_name": "destination_original"})
    original_address = rpc_call(harness.gui_rpc_port, "getnewaddress", wallet="destination_original")
    selected = Path(harness.tmpdir) / "archive.v1"
    final = Path(str(selected) + ".bak")
    rpc_call(harness.gui_rpc_port, "backupwallet", [str(final)], wallet="destination_original")
    original_bytes = final.read_bytes()
    selected.write_bytes(b"selected sentinel")
    rpc_call(harness.gui_rpc_port, "createwallet", {"wallet_name": "destination_replacement"})
    replacement_address = rpc_call(harness.gui_rpc_port, "getnewaddress", wallet="destination_replacement")
    select_wallet(gui, "destination_replacement")
    open_wallet_settings(gui)
    gui.set_text("walletSettingsBackupPathField", str(selected))
    gui.click("walletBackupRow")
    gui.wait_for_property("walletBackupOverwritePopup", "visible", True)
    assert gui.get_property("walletBackupFinalPathText", "text") == str(final)
    gui.click("walletBackupOverwriteRejectButton")
    gui.wait_for_property("walletBackupRow", "enabled", True)
    assert selected.read_bytes() == b"selected sentinel" and final.read_bytes() == original_bytes
    checkpoint("declining the normalized destination preserved both files")

    # A direct model request follows exactly the same policy as a native chooser.
    gui.set_text("walletSettingsBackupPathField", str(selected))
    gui.click("walletBackupRow")
    gui.wait_for_property("walletBackupOverwritePopup", "visible", True)
    changed_bytes = original_bytes + b"external change while confirmation is open"
    final.write_bytes(changed_bytes)
    gui.click("walletBackupOverwriteApproveButton")
    gui.wait_for_property("walletBackupRow", "enabled", True)
    assert "destination changed" in gui.get_property("walletSettingsPage", "errorText").lower()
    assert final.read_bytes() == changed_bytes and selected.read_bytes() == b"selected sentinel"
    checkpoint("stale overwrite approval refused after an independent file change")

    final.write_bytes(original_bytes)
    gui.set_text("walletSettingsBackupPathField", str(selected))
    gui.click("walletBackupRow")
    gui.wait_for_property("walletBackupOverwritePopup", "visible", True)
    gui.click("walletBackupOverwriteApproveButton")
    gui.wait_for_property("walletBackupRow", "supportingText", "Backup saved.", timeout_ms=30000)
    assert final.read_bytes() != original_bytes and selected.read_bytes() == b"selected sentinel"
    rpc_call(harness.gui_rpc_port, "restorewallet", ["destination_restored", str(final)])
    assert rpc_call(harness.gui_rpc_port, "getaddressinfo", [replacement_address], wallet="destination_restored")["ismine"]
    assert not rpc_call(harness.gui_rpc_port, "getaddressinfo", [original_address], wallet="destination_restored")["ismine"]
    checkpoint("approved replacement restored independently with the replacement wallet keys")

    open_wallet_selector(gui)
    gui.click("walletSelectItem_destination_replacement")
    gui.wait_for_property("walletBadge", "text", "destination_replacement", timeout_ms=30000)
    if not gui.get_property("desktopWalletSettingsTabButton", "checked"):
        open_wallet_settings(gui)
    gui.wait_for_page("walletSettingsPage")
    gui.wait_for_property("walletNameRow", "value", "destination_replacement", timeout_ms=30000)
    new_selected = Path(harness.tmpdir) / "new.archive"
    new_final = Path(str(new_selected) + ".bak")
    gui.set_text("walletSettingsBackupPathField", str(new_selected))
    gui.click("walletBackupRow")
    gui.wait_for_property("walletBackupOverwritePopup", "visible", True)
    assert gui.get_property("walletBackupFinalPathText", "text") == str(new_final)
    assert not new_selected.exists() and not new_final.exists()
    gui.click("walletBackupOverwriteApproveButton")
    gui.wait_for_property("walletBackupRow", "supportingText", "Backup saved.", timeout_ms=30000)
    assert not new_selected.exists() and new_final.stat().st_size > 0
    checkpoint("new rewritten destination required confirmation before the background write")


if __name__ == "__main__":
    raise SystemExit(run_scenarios('Wallet management process journeys', [
        wallet_scenario("backup-reload-restore", 4, wallet_smoke),
        wallet_scenario("password-backup-recovery", 4, backup_recovery_disclosure),
        wallet_scenario("backup-final-destination", 4, backup_final_destination),
    ]))
