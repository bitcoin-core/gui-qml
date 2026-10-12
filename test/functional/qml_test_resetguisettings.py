#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Exercise reset GUI settings semantics across onboarding restarts."""

import json
import os
import shutil
import sys
import tempfile
import time

from qml_process_checks import check_gui_exit
from qml_test_harness import (
    QmlTestHarness,
    complete_preinit_onboarding,
    dump_qml_tree,
    parse_args,
)


def click_to_storage_location(gui):
    gui.wait_for_page("onboardingCover", timeout_ms=10000)
    for button, expected_page in [
        ("onboardingCoverButton", "onboardingStrengthen"),
        ("onboardingStrengthenButton", "onboardingBlockchain"),
        ("onboardingBlockchainButton", "onboardingBlockclock"),
        ("onboardingBlockclockButton", "onboardingStorageLocation"),
    ]:
        gui.wait_for_property(button, "enabled", True, timeout_ms=5000)
        gui.click(button)
        gui.wait_for_page(expected_page, timeout_ms=5000)


def select_custom_datadir(gui, datadir):
    gui.wait_for_page("onboardingStorageLocation", timeout_ms=5000)
    assert gui.invoke_property_object(
        "onboardingStorageLocation",
        "settingsModel",
        "selectCustomDataDir",
        [datadir],
    )
    gui.wait_for_property(
        "onboardingStorageLocationButton",
        "enabled",
        True,
        timeout_ms=10000,
    )


def click_to_connection(gui):
    gui.wait_for_property("onboardingStorageLocationButton", "enabled", True, timeout_ms=10000)
    gui.click("onboardingStorageLocationButton")
    gui.wait_for_page("onboardingStorageAmount", timeout_ms=5000)
    gui.wait_for_property("onboardingStorageAmountButton", "enabled", True, timeout_ms=10000)
    gui.click("onboardingStorageAmountButton")
    gui.wait_for_page("onboardingConnection", timeout_ms=5000)


def open_connection_settings(gui):
    gui.click("connectionSettingsButton")
    gui.wait_for_page("proxySettingsRow", timeout_ms=5000)


def open_proxy_settings(gui):
    gui.click("proxySettingsRow")
    gui.wait_for_page("proxySettingsPage", timeout_ms=5000)


def close_proxy_settings(gui):
    gui.wait_for_property("proxySettingsSaveButton", "enabled", True, timeout_ms=2000)
    gui.click("proxySettingsSaveButton")
    gui.wait_for_page("proxySettingsRow", timeout_ms=5000)


def close_connection_settings(gui):
    gui.click("onboardingConnectionSettingsCloseButton")
    gui.wait_for_property("onboardingConnectionSettingsPopup", "visible", False, timeout_ms=5000)
    gui.wait_for_page("onboardingConnectionButton", timeout_ms=5000)


def set_switch(gui, object_name, desired):
    if gui.get_property(object_name, "checked") != desired:
        gui.click(object_name)
        gui.wait_for_property(object_name, "checked", desired, timeout_ms=2000)


def load_settings(datadir):
    settings_path = os.path.join(datadir, "regtest", "settings.json")
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if os.path.exists(settings_path):
            with open(settings_path, encoding="utf8") as settings_file:
                return json.load(settings_file)
        time.sleep(0.1)
    raise AssertionError(f"Timed out waiting for {settings_path}")


def run_window_behavior_reset(hide_tray_icon):
    # Select regtest only through the harness's bitcoin.conf. An explicit
    # -regtest would let the early reset clear the right store and hide a
    # regression in the later reset after the configuration has been read.
    with tempfile.TemporaryDirectory(prefix="qml_reset_window_") as tmpdir:
        for phase in ("save", "reload", "reset"):
            harness = QmlTestHarness(
                tmpdir=tmpdir,
                reset_settings=phase == "reset",
                extra_args=["-disablewallet"],
            )
            try:
                harness.start()
                gui = harness.driver
                if phase == "reset":
                    complete_preinit_onboarding(gui)
                    gui = harness.wait_for_main_window_reconnect()
                gui.wait_for_page("nodeSettingsButton", timeout_ms=30000)
                gui.click("nodeSettingsButton")
                gui.wait_for_page("settingsSidebar_window-behavior", timeout_ms=5000)
                gui.click("settingsSidebar_window-behavior")
                gui.wait_for_page("windowBehaviorSettingsPage", timeout_ms=5000)

                # Write through the GUI so Qt chooses the platform's effective
                # organization and the redirected -test-settings-dir store.
                if phase == "save":
                    set_switch(gui, "showTrayIconSwitch", not hide_tray_icon)
                    set_switch(gui, "minimizeToTraySwitch", not hide_tray_icon)
                    set_switch(gui, "minimizeOnCloseSwitch", True)
                expected = {
                    "showTrayIconSwitch": phase == "reset" or not hide_tray_icon,
                    "minimizeToTraySwitch": phase != "reset" and not hide_tray_icon,
                    "minimizeOnCloseSwitch": phase != "reset",
                }
                for name, checked in expected.items():
                    actual = gui.get_property(name, "checked")
                    assert actual == checked, f"{phase}: {name} is {actual}, expected {checked}"

                gui.request_quit()
                harness.process.wait(timeout=30)
                _, stderr = harness.process.communicate(timeout=5)
                check_gui_exit(harness.process.returncode, stderr.decode("utf8", errors="replace"), socket_path=harness.socket_path)
            finally:
                harness.stop(cleanup=False)


def run_first_reset_onboarding(tmpdir, custom_datadir):
    harness = QmlTestHarness(
        use_datadir_arg=False,
        reset_settings=True,
        tmpdir=tmpdir,
        no_listen_arg=False,
        extra_args=["-regtest", "-disablewallet"],
    )
    gui = None
    try:
        harness.start()
        gui = harness.driver
        click_to_storage_location(gui)
        select_custom_datadir(gui, custom_datadir)
        click_to_connection(gui)
        open_connection_settings(gui)

        set_switch(gui, "listenSwitch", False)
        set_switch(gui, "natpmpSwitch", True)
        set_switch(gui, "serverSwitch", True)
        open_proxy_settings(gui)
        set_switch(gui, "proxyEnableSwitch", True)
        gui.set_text("proxyAddressInput", "10.0.0.1:9050")
        gui.wait_for_property("proxySettingsPage", "draftProxyAddress", "10.0.0.1:9050", timeout_ms=2000)
        gui.wait_for_property("proxySettingsPage", "draftProxyValidationError", "", timeout_ms=2000)
        set_switch(gui, "torEnableSwitch", True)
        gui.set_text("torAddressInput", "127.0.0.1:9150")
        gui.wait_for_property("proxySettingsPage", "draftTorAddress", "127.0.0.1:9150", timeout_ms=2000)
        gui.wait_for_property("proxySettingsPage", "draftTorValidationError", "", timeout_ms=2000)
        close_proxy_settings(gui)
        close_connection_settings(gui)

        gui.click("onboardingConnectionButton")
        harness.wait_for_main_window_reconnect()

        settings = load_settings(custom_datadir)
        assert settings.get("listen") is False, settings
        assert "natpmp" not in settings, settings
        assert settings.get("server") is True, settings
        assert settings.get("proxy") == "10.0.0.1:9050", settings
        assert settings.get("onion") == "127.0.0.1:9150", settings
    except Exception:
        if gui is not None:
            dump_qml_tree(gui)
        raise
    finally:
        harness.stop(cleanup=False)


def run_second_reset_onboarding(tmpdir, custom_datadir):
    harness = QmlTestHarness(
        use_datadir_arg=False,
        reset_settings=True,
        tmpdir=tmpdir,
        no_listen_arg=False,
        extra_args=["-regtest", "-disablewallet"],
    )
    gui = None
    try:
        harness.start()
        gui = harness.driver
        click_to_storage_location(gui)
        select_custom_datadir(gui, custom_datadir)
        click_to_connection(gui)
        open_connection_settings(gui)

        assert gui.get_property("listenSwitch", "checked") is True
        assert gui.get_property("natpmpSwitch", "checked") is True
        assert gui.get_property("serverSwitch", "checked") is False
        open_proxy_settings(gui)
        assert gui.get_property("proxyEnableSwitch", "checked") is False
        assert gui.get_property("torEnableSwitch", "checked") is False
    except Exception:
        if gui is not None:
            dump_qml_tree(gui)
        raise
    finally:
        harness.stop(cleanup=False)


def run_tests():
    args = parse_args()
    if args.socket_path:
        raise RuntimeError("qml_test_resetguisettings.py must launch the app itself")

    run_window_behavior_reset(hide_tray_icon=False)
    run_window_behavior_reset(hide_tray_icon=True)

    tmpdir = tempfile.mkdtemp(prefix="qml_resetguisettings_")
    custom_datadir = os.path.join(tmpdir, "custom-data-dir")
    os.makedirs(custom_datadir, exist_ok=True)
    try:
        run_first_reset_onboarding(tmpdir, custom_datadir)
        run_second_reset_onboarding(tmpdir, custom_datadir)
        print("\n" + "=" * 50)
        print("All tests PASSED")
        print("=" * 50)
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)


if __name__ == "__main__":
    try:
        run_tests()
    except Exception as e:
        print(f"\nFAILED: {e}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        sys.exit(1)
