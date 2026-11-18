#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Persist node settings and block-clock history across one shared GUI restart."""

import time
import configparser
import json
from pathlib import Path
from qml_scenarios import run_scenarios
from qml_smoke_lib import (
    stop_gui_checked,
    wait_until,
    wallet_scenario,
)
from qml_wallet_test_lib import (
    rpc_call,
    wait_for_rpc,
)


def open_settings(gui, section):
    desktop = gui.get_property("appWindow", "desktopWalletMode")
    # Reuse the current settings page. Clicking the hidden node button pushes
    # another SettingsView and leaves duplicate sections in the navigation tree.
    if not gui.object_exists("settingsView") or not gui.get_property("settingsView", "visible"):
        if desktop:
            gui.click("desktopWalletSettingsTabButton")
            gui.wait_for_property("desktopWalletSettingsTabButton", "checked", True)
        else:
            gui.click("nodeSettingsButton")
    gui.wait_for_page("settingsView")
    if not desktop:
        # The node stack is nested inside the outer mainPageStack.
        gui.wait_for_property("mainPageStack", "currentItem.busy", False)
        assert gui.get_property("mainPageStack", "currentItem.depth") == 2
    gui.wait_for_property("settingsView", "optionsEditable", True)
    gui.click(f"settingsSidebar_{section}")
    gui.wait_for_property("settingsView", "selectedSectionId", section)
    gui.wait_for_property("settingsView", "pageContainer.currentSectionId", section)
    gui.settle(stack_view_names=("mainPageStack", f"settingsNavigationStack_{section}"))


def open_connection_settings(gui):
    open_settings(gui, "connection")
    gui.wait_for_page("connectionSettingsPage")
    gui.wait_for_property("settingsView", "navigation.editor.fresh", True, timeout_ms=30000)
    gui.wait_for_property("settingsView", "navigation.editor.busy", False, timeout_ms=30000)


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

    open_connection_settings(gui)
    expected_proxies = {"proxy": "127.0.0.1:9050", "onion": "127.0.0.1:9150"}
    for key, value in expected_proxies.items():
        field = key + "SettingInput"
        gui.wait_for_property(field, "enabled", True)
        gui.wait_for_property(field, "text", "")
        gui.set_text(field, value)
        gui.wait_for_property("settingsView", "navigation.editor.inputPending", False, timeout_ms=30000)
        assert gui.get_property("effectiveSetting_" + key, "entry.error") in (None, "")
    gui.wait_for_property("settingsSaveBatchButton", "enabled", True)
    editor_id = gui.get_property("settingsView", "navigation.editor.editorId")
    # lastSaveId is the token in saveResult.operationId. Only this one Save
    # is requested, so its original receipt must have the next editor token.
    save_token = str(int(gui.get_property("settingsView", "navigation.editor.lastSaveId")) + 1)
    gui.click("settingsSaveBatchButton")
    def original_proxy_saved():
        assert gui.get_property("settingsView", "navigation.editor.editorId") == editor_id
        observed = gui.get_property("settingsView", "navigation.editor.lastSaveId")
        if observed != save_token:
            assert int(observed) < int(save_token), "Another save replaced the original settings receipt"
            return False
        if gui.get_property("settingsView", "navigation.editor.busy"):
            return False
        assert gui.get_property("settingsView", "navigation.editor.lastSavePresent")
        assert gui.get_property("settingsView", "navigation.editor.lastSaveKnown")
        assert gui.get_property("settingsView", "navigation.editor.lastSavePersisted")
        assert gui.get_property("settingsView", "navigation.editor.error") in (None, "")
        assert gui.get_property("settingsView", "navigation.editor.lastSaveId") == save_token
        return True
    wait_until(original_proxy_saved, "the original proxy settings batch Save receipt")
    actual_values = gui.get_property("settingsActualSaveResult", "text").splitlines()
    saved = json.loads(Path(harness.gui_settings_path).read_text(encoding="utf8"))
    for key, value in expected_proxies.items():
        assert f"{key} = {value}" in actual_values, actual_values
        assert saved[key] == value, saved
    checkpoint("proxy settings saved")

    open_settings(gui, "window-behavior")
    gui.wait_for_page("windowBehaviorSettingsPage")
    gui.wait_for_property("showTrayIconSwitch", "enabled", True)
    gui.click("showTrayIconSwitch")
    gui.wait_for_property("showTrayIconSwitch", "checked", False)
    # Each preference write temporarily closes admission while its actual
    # readback is pending. Dispatch the next physical action after it reopens.
    gui.wait_for_property("windowBehaviorSettingsPage", "windowBehaviorModel.persistencePending", False)
    gui.wait_for_property("windowBehaviorSettingsPage", "windowBehaviorModel.persistenceError", "")
    gui.wait_for_property("minimizeOnCloseSwitch", "enabled", True)
    gui.click("minimizeOnCloseSwitch")
    gui.wait_for_property("minimizeOnCloseSwitch", "checked", True)
    gui.wait_for_property("windowBehaviorSettingsPage", "windowBehaviorModel.persistencePending", False)
    gui.wait_for_property("windowBehaviorSettingsPage", "windowBehaviorModel.persistenceError", "")
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
    # The language page belongs to the display section's own navigation stack.
    gui.settle(stack_view_names=("mainPageStack", "settingsNavigationStack_display"))
    gui.wait_for_property("displayLanguageRow", "enabled", True)
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
    open_connection_settings(gui)
    for key, value in expected_proxies.items():
        gui.wait_for_property(key + "SettingInput", "text", value)
        gui.wait_for_property("effectiveSetting_" + key, "entry.effectiveText", value)
    networks = {network["name"]: network for network in rpc_call(harness.gui_rpc_port, "getnetworkinfo")["networks"]}
    assert networks["ipv4"]["proxy"] == expected_proxies["proxy"], networks
    assert networks["ipv6"]["proxy"] == expected_proxies["proxy"], networks
    assert networks["onion"]["proxy"] == expected_proxies["onion"], networks
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


def clamped_geometry_restart(harness, checkpoint, *, wallet):
    """Original 044 tiny/removed-display rectangle, in both real shells."""
    settings = Path(harness.config_home) / "BitcoinCore"
    settings.mkdir(parents=True)
    (settings / "BitcoinCore-App-regtest.ini").write_text(
        "[General]\nwindowWidth=200\nwindowHeight=100\nwindowX=20000\nwindowY=-20000\n", encoding="utf8")
    arguments = [] if wallet else ["-disablewallet"]
    harness.start_gui(extra_args=arguments)
    gui = harness.driver
    gui.wait_for_property("appWindow", "preferenceGeometryRestored", True)
    gui.wait_for_property("appWindow", "width", 800)
    gui.wait_for_property("appWindow", "height", 665)
    rectangle = {key: gui.get_property("appWindow", key) for key in ("x", "y", "width", "height")}
    assert rectangle["x"] != 20000 and rectangle["y"] != -20000, rectangle
    checkpoint("untrusted saved rectangle restored at the supported minimum on an available display")
    stop_gui_checked(harness)
    persisted = configparser.ConfigParser()
    persisted.read(settings / "BitcoinCore-App-regtest.ini", encoding="utf8")
    for key, value in rectangle.items():
        assert int(persisted["General"]["window" + key.capitalize()]) == value, (key, value, dict(persisted["General"]))
    harness.start_gui(extra_args=arguments)
    gui = harness.driver
    gui.wait_for_property("appWindow", "preferenceGeometryRestored", True)
    for key, value in rectangle.items():
        gui.wait_for_property("appWindow", key, value)
    checkpoint("clamped native rectangle persisted and restored across process restart")


def wallet_clamped_geometry_restart(harness, checkpoint):
    clamped_geometry_restart(harness, checkpoint, wallet=True)


def node_clamped_geometry_restart(harness, checkpoint):
    clamped_geometry_restart(harness, checkpoint, wallet=False)


if __name__ == "__main__":
    raise SystemExit(run_scenarios('Node settings and history restart', [
        wallet_scenario("settings-and-history-restart", 2, node_smoke),
        wallet_scenario("wallet-shell-geometry", 4, wallet_shell_geometry),
        wallet_scenario("wallet-clamped-geometry-restart", 6, wallet_clamped_geometry_restart),
        wallet_scenario("node-clamped-geometry-restart", 6, node_clamped_geometry_restart),
    ]))
