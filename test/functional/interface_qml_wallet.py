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
    backup = directory / f"{name}.bak"
    gui.wait_for_property("walletSettingsPage", "errorText", "")
    assert backup.is_file() and backup.stat().st_size > 0
    checkpoint("wallet backup created")

    close_wallet(harness, name)
    stop_gui_checked(harness)
    harness.start_gui()
    gui = harness.driver
    gui.wait_for_property("walletBadge", "loading", False, timeout_ms=30000)
    open_wallet_selector(gui)
    gui.click(f"walletSelectItem_{name}")
    gui.wait_for_property("walletBadge", "text", name, timeout_ms=30000)
    checkpoint("closed wallet loaded after restart")

    close_wallet(harness, name)
    restored = Path(harness.tmpdir) / "restored_smoke.bak"
    shutil.copyfile(backup, restored)
    import_wallet(harness, restored)
    assert rpc_call(harness.gui_rpc_port, "getaddressinfo", [address], wallet=restored.stem)["ismine"]
    checkpoint("backup restored through the wallet wizard")

if __name__ == "__main__":
    raise SystemExit(run_scenarios('Wallet management process journeys', [
        wallet_scenario("backup-reload-restore", 4, wallet_smoke),
    ]))
