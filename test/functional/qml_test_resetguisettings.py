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

from qml_test_harness import (
    QmlTestHarness,
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
