#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Persist node settings and block-clock history across one shared GUI restart."""

import time
from pathlib import Path
from qml_scenarios import run_scenarios
from qml_smoke_lib import (
    stop_gui_checked,
    wallet_scenario,
)
from qml_wallet_test_lib import (
    rpc_call,
    wait_for_rpc,
)


def open_settings(gui, section):
    if gui.get_property("appWindow", "desktopWalletMode"):
        gui.click("desktopWalletSettingsTabButton")
        gui.wait_for_property("desktopWalletSettingsTabButton", "checked", True)
    else:
        gui.click("nodeSettingsButton")
    gui.click(f"settingsSidebar_{section}")
    gui.settle()


def open_proxy(gui):
    open_settings(gui, "connection")
    gui.wait_for_page("proxySettingsRow")
    gui.click("proxySettingsRow")
    gui.wait_for_page("proxySettingsPage")


def node_smoke(harness, checkpoint):
    """Persist display, proxy and window settings through the GUI."""
    # Restoring non-default geometry during wallet-shell construction previously
    # crashed Qt 6.4 before the automation bridge could connect.
    settings_dir = Path(harness.config_home) / "BitcoinCore"
    settings_dir.mkdir(parents=True)
    (settings_dir / "BitcoinCore-App-regtest.ini").write_text(
        "[General]\nwindowWidth=1200\nwindowHeight=800\nwindowX=120\nwindowY=130\n",
        encoding="utf8",
    )
    harness.start_gui(extra_args=["-disablewallet"])
    gui = harness.driver
    wait_for_rpc(harness.gui_rpc_port)
    gui.wait_for_property("appWindow", "width", 1200)
    gui.wait_for_property("appWindow", "height", 800)
    checkpoint("wallet shell started with saved geometry")

    open_proxy(gui)
    for switch, field, value in (
        ("proxyEnableSwitch", "proxyAddressInput", "127.0.0.1:9050"),
        ("torEnableSwitch", "torAddressInput", "127.0.0.1:9150"),
    ):
        gui.click(switch)
        gui.wait_for_property(field, "enabled", True)
        gui.set_text(field, value)
    gui.wait_for_property("proxySettingsSaveButton", "enabled", True)
    gui.click("proxySettingsSaveButton")
    gui.wait_for_page("proxySettingsRow")
    checkpoint("proxy settings saved")

    open_settings(gui, "window-behavior")
    gui.wait_for_page("windowBehaviorSettingsPage")
    gui.click("showTrayIconSwitch")
    gui.wait_for_property("showTrayIconSwitch", "checked", False)
    gui.click("minimizeOnCloseSwitch")
    gui.wait_for_property("minimizeOnCloseSwitch", "checked", True)
    gui.close_window()
    assert harness.gui_process.poll() is None
    assert rpc_call(harness.gui_rpc_port, "getblockcount") == 0
    gui.set_property("appWindow", "visible", True)
    checkpoint("closing the window kept the node running")

    open_settings(gui, "display")
    gui.wait_for_page("displaySettingsPage")
    gui.click("displayUnitPickerButton")
    gui.wait_for_property("displayUnitSAT", "visible", True)
    gui.click("displayUnitSAT")
    gui.wait_for_property("displayUnitPicker", "currentValue", 3)
    gui.wait_for_property("displayUnitPickerMenu", "visible", False)
    gui.settle()
    gui.click("displayLanguageRow")
    gui.wait_for_page("settingsLanguagePage")
    gui.set_text("languageSearch", "español")
    gui.click("language_es")
    gui.wait_for_property("displayLanguageRow", "title", "Idioma")
    checkpoint("display unit and language selected")

    descriptor = rpc_call(harness.gui_rpc_port, "getdescriptorinfo", ["raw(51)"])["descriptor"]
    rpc_call(harness.gui_rpc_port, "setmocktime", [1300000000])
    rpc_call(harness.gui_rpc_port, "generatetodescriptor", [100, descriptor])
    timestamp = int(time.time())
    rpc_call(harness.gui_rpc_port, "setmocktime", [timestamp])
    blocks = rpc_call(harness.gui_rpc_port, "generatetodescriptor", [3, descriptor])
    rpc_call(harness.gui_rpc_port, "invalidateblock", [blocks[1]])
    rpc_call(harness.gui_rpc_port, "setmocktime", [timestamp + 1])
    rpc_call(harness.gui_rpc_port, "generatetodescriptor", [2, descriptor])
    checkpoint("chain history prepared before restart")

    stop_gui_checked(harness)
    harness.start_gui(extra_args=["-disablewallet"])
    gui = harness.driver
    wait_for_rpc(harness.gui_rpc_port)
    open_settings(gui, "display")
    gui.wait_for_property("displayUnitPicker", "currentValue", 3)
    gui.wait_for_property("displayLanguageRow", "title", "Idioma")
    open_settings(gui, "window-behavior")
    gui.wait_for_property("showTrayIconSwitch", "checked", False)
    gui.wait_for_property("minimizeOnCloseSwitch", "checked", True)
    open_proxy(gui)
    gui.wait_for_property("proxyEnableSwitch", "checked", True)
    gui.wait_for_property("proxyAddressInput", "text", "127.0.0.1:9050")
    gui.wait_for_property("torEnableSwitch", "checked", True)
    gui.wait_for_property("torAddressInput", "text", "127.0.0.1:9150")
    checkpoint("settings restored after restart")
    gui.click("settingsDoneButton")
    gui.wait_for_page("blockClock")
    def restored(fractions):
        period = gui.get_property("blockClock", "blockClockModelRef.periodStart")
        expected = sorted((value - period) / 43200 for value in (timestamp, timestamp + 1, timestamp + 1)
                          if period <= value < period + 43200)
        return isinstance(fractions, list) and len(fractions) == len(expected) and all(
            abs(actual - wanted) < 1e-10 for actual, wanted in zip(fractions, expected))
    gui.wait_for_property("blockClockDial", "blockTimeFractions", restored)
    checkpoint("block history restored without a new tip")


def wallet_shell_geometry(harness, checkpoint):
    """The saved geometry regression specifically needs the wallet shell."""
    settings = Path(harness.config_home) / "BitcoinCore"
    settings.mkdir(parents=True)
    (settings / "BitcoinCore-App-regtest.ini").write_text(
        "[General]\nwindowWidth=1200\nwindowHeight=800\nwindowX=120\nwindowY=130\n", encoding="utf8")
    harness.start_gui()
    harness.driver.wait_for_property("appWindow", "width", 1200)
    harness.driver.wait_for_property("appWindow", "height", 800)
    checkpoint("wallet shell started with saved geometry")


if __name__ == "__main__":
    raise SystemExit(run_scenarios('Node settings and history restart', [
        wallet_scenario("settings-and-history-restart", 2, node_smoke),
        wallet_scenario("wallet-shell-geometry", 4, wallet_shell_geometry),
    ]))
