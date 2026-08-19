#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Functional test for the runtime proxy settings UI.

Tests navigation to the proxy settings page from the runtime settings shell,
toggling the default proxy and Tor proxy enable switches, entering valid and
invalid addresses, discarding draft edits with Back, and verifying that settings
persist to disk only after pressing Done.

This test requires the binary to be built with -DENABLE_TEST_AUTOMATION=ON.
"""

import json
import os
import sys
import time

from qml_driver import QmlDriverError
from qml_test_harness import (
    QmlTestHarness,
    complete_preinit_onboarding,
    dump_qml_tree,
    parse_args,
)


def navigate_to_proxy_settings(gui):
    """Navigate from the runtime settings shell to the Proxy Settings page."""
    if gui.object_exists("desktopWalletSettingsTabButton"):
        gui.wait_for_property("desktopWalletSettingsTabButton", "visible", True, timeout_ms=10000)
        gui.click("desktopWalletSettingsTabButton")
    else:
        gui.wait_for_page("nodeSettingsButton", timeout_ms=10000)
        gui.click("nodeSettingsButton")

    gui.settle()
    gui.wait_for_property("settingsSidebar_connection", "visible", True, timeout_ms=5000)
    gui.click("settingsSidebar_connection")
    gui.wait_for_page("settingsv2ProxySettingsRow", timeout_ms=5000)
    gui.click("settingsv2ProxySettingsRow")
    gui.wait_for_page("settingsv2ProxySettingsPage", timeout_ms=5000)
    print("  Navigated to Proxy Settings page.")


def leave_proxy_settings_with_done(gui):
    """Commit draft proxy settings and return to Connection settings."""
    gui.wait_for_property("settingsv2ProxySettingsSaveButton", "enabled", True, timeout_ms=2000)
    gui.click("settingsv2ProxySettingsSaveButton")
    gui.wait_for_page("settingsv2ProxySettingsRow", timeout_ms=5000)


def navigate_back_from_connection_settings(gui):
    """Navigate back from Connection settings to the runtime settings shell."""
    if gui.object_exists("settingsv2SettingsDoneButton"):
        gui.click("settingsv2SettingsDoneButton")
    else:
        gui.click("activityTabButton")
    gui.settle()
    if gui.object_exists("desktopWalletSettingsTabButton"):
        gui.wait_for_property("desktopWalletSettingsTabButton", "visible", True, timeout_ms=5000)
    else:
        gui.wait_for_page("nodeSettingsButton", timeout_ms=5000)
    print("  Navigated back to runtime settings.")


def test_default_proxy_toggle(gui):
    print("\n── test_default_proxy_toggle ──────────────────────────────────────")

    # Proxy should be disabled by default (fresh datadir, no prior config).
    checked = gui.get_property("settingsv2ProxyEnableSwitch", "checked")
    assert not checked, f"Expected proxy disabled by default, got checked={checked}"

    dirty = gui.get_property("settingsv2ProxyRestartNotice", "visible")
    assert not dirty, "Expected proxySettingsDirty=False before any change"
    draft_dirty = gui.get_property("settingsv2ProxySettingsPage", "proxyDraftDirty")
    assert not draft_dirty, "Expected proxyDraftDirty=False before any change"

    # Enable proxy.
    gui.click("settingsv2ProxyEnableSwitch")
    gui.wait_for_property("settingsv2ProxyEnableSwitch", "checked", True, timeout_ms=2000)
    checked = gui.get_property("settingsv2ProxyEnableSwitch", "checked")
    assert checked, "Expected proxyEnableSwitch to be checked after click"
    print("  Default proxy toggled ON: OK")

    gui.wait_for_property("settingsv2ProxySettingsPage", "proxyDraftDirty", True, timeout_ms=2000)
    dirty = gui.get_property("settingsv2ProxyRestartNotice", "visible")
    assert not dirty, "Expected proxySettingsDirty=False before pressing Done"
    print("  Proxy edit is draft-only before Done: OK")

    # Disable proxy.
    gui.click("settingsv2ProxyEnableSwitch")
    gui.wait_for_property("settingsv2ProxyEnableSwitch", "checked", False, timeout_ms=2000)
    checked = gui.get_property("settingsv2ProxyEnableSwitch", "checked")
    assert not checked, "Expected proxyEnableSwitch to be unchecked after second click"
    print("  Default proxy toggled OFF: OK")

    gui.wait_for_property("settingsv2ProxySettingsPage", "proxyDraftDirty", False, timeout_ms=2000)
    print("  proxyDraftDirty=False after reverting proxy change: OK")


def test_proxy_valid_address(gui):
    print("\n── test_proxy_valid_address ────────────────────────────────────────")

    # Enable proxy so the address field becomes active.
    if not gui.get_property("settingsv2ProxyEnableSwitch", "checked"):
        gui.click("settingsv2ProxyEnableSwitch")
        gui.wait_for_property("settingsv2ProxyEnableSwitch", "checked", True, timeout_ms=2000)

    gui.wait_for_property("settingsv2ProxyAddressInput", "enabled", True, timeout_ms=2000)
    gui.set_text("settingsv2ProxyAddressInput", "")
    gui.wait_for_property("settingsv2ProxyAddressInput", "text", "", timeout_ms=2000)
    gui.invoke("settingsv2ProxyAddressInput", "forceActiveFocus")
    gui.wait_for_property("settingsv2ProxyAddressInput", "activeFocus", True, timeout_ms=2000)
    gui.type_text("settingsv2ProxyAddressInput", "10.0.0.1:9050")
    gui.wait_for_property("settingsv2ProxyAddressInput", "text", "10.0.0.1:9050", timeout_ms=2000)
    gui.wait_for_property("settingsv2ProxySettingsPage", "draftProxyValidationError", "", timeout_ms=2000)
    gui.wait_for_property("settingsv2ProxySettingsSaveButton", "enabled", True, timeout_ms=2000)
    print("  Valid address accepted: OK")


def test_proxy_invalid_address(gui):
    print("\n── test_proxy_invalid_address ──────────────────────────────────────")

    # Enable proxy so the address field becomes active.
    if not gui.get_property("settingsv2ProxyEnableSwitch", "checked"):
        gui.click("settingsv2ProxyEnableSwitch")
        gui.wait_for_property("settingsv2ProxyEnableSwitch", "checked", True, timeout_ms=2000)

    gui.wait_for_property("settingsv2ProxyAddressInput", "enabled", True, timeout_ms=2000)
    # Enter an address with invalid IP octets.
    gui.set_text("settingsv2ProxyAddressInput", "999.999.999.999:9050")
    gui.wait_for_property(
        "settingsv2ProxySettingsPage",
        "draftProxyValidationError",
        lambda error: len(error) > 0,
        timeout_ms=2000,
    )
    gui.wait_for_property("settingsv2ProxySettingsSaveButton", "enabled", False, timeout_ms=2000)
    print("  Invalid address rejected: OK")

    # Restore to a valid address for subsequent tests.
    gui.set_text("settingsv2ProxyAddressInput", "127.0.0.1:9050")
    gui.wait_for_property("settingsv2ProxySettingsPage", "draftProxyValidationError", "", timeout_ms=2000)
    gui.wait_for_property("settingsv2ProxySettingsSaveButton", "enabled", True, timeout_ms=2000)


def test_tor_proxy_toggle(gui):
    print("\n── test_tor_proxy_toggle ───────────────────────────────────────────")

    # Tor proxy should be disabled by default.
    checked = gui.get_property("settingsv2TorEnableSwitch", "checked")
    assert not checked, f"Expected Tor proxy disabled by default, got checked={checked}"

    # Enable Tor proxy.
    gui.click("settingsv2TorEnableSwitch")
    gui.wait_for_property("settingsv2TorEnableSwitch", "checked", True, timeout_ms=2000)
    gui.wait_for_property("settingsv2TorAddressInput", "enabled", True, timeout_ms=2000)
    checked = gui.get_property("settingsv2TorEnableSwitch", "checked")
    assert checked, "Expected torEnableSwitch to be checked after click"
    print("  Tor proxy toggled ON: OK")

    # Disable Tor proxy.
    gui.click("settingsv2TorEnableSwitch")
    gui.wait_for_property("settingsv2TorEnableSwitch", "checked", False, timeout_ms=2000)
    checked = gui.get_property("settingsv2TorEnableSwitch", "checked")
    assert not checked, "Expected torEnableSwitch to be unchecked after second click"
    print("  Tor proxy toggled OFF: OK")


def test_back_discards_proxy_draft(gui):
    print("\n── test_back_discards_proxy_draft ─────────────────────────────────")

    if not gui.get_property("settingsv2ProxyEnableSwitch", "checked"):
        gui.click("settingsv2ProxyEnableSwitch")
        gui.wait_for_property("settingsv2ProxyEnableSwitch", "checked", True, timeout_ms=2000)

    gui.wait_for_property("settingsv2ProxyAddressInput", "enabled", True, timeout_ms=2000)
    gui.set_text("settingsv2ProxyAddressInput", "10.0.0.5:9050")
    gui.wait_for_property("settingsv2ProxyAddressInput", "text", "10.0.0.5:9050", timeout_ms=2000)
    gui.wait_for_property("settingsv2ProxySettingsPage", "proxyDraftDirty", True, timeout_ms=2000)
    dirty = gui.get_property("settingsv2ProxyRestartNotice", "visible")
    assert not dirty, "Expected model to remain unchanged before pressing Done"

    gui.click("settingsv2ProxySettingsBackButton")
    gui.wait_for_property("settingsv2DiscardProxyChangesPopup", "visible", True, timeout_ms=2000)
    gui.click("settingsv2DiscardProxyChangesCancelButton")
    gui.wait_for_property("settingsv2DiscardProxyChangesPopup", "visible", False, timeout_ms=2000)
    gui.wait_for_property("settingsv2ProxyAddressInput", "text", "10.0.0.5:9050", timeout_ms=2000)
    print("  Back cancellation keeps draft changes: OK")

    gui.click("settingsv2ProxySettingsBackButton")
    gui.wait_for_property("settingsv2DiscardProxyChangesPopup", "visible", True, timeout_ms=2000)
    gui.click("settingsv2DiscardProxyChangesConfirmButton")
    gui.wait_for_page("settingsv2ProxySettingsRow", timeout_ms=5000)
    gui.click("settingsv2ProxySettingsRow")
    gui.wait_for_page("settingsv2ProxySettingsPage", timeout_ms=5000)
    gui.wait_for_property("settingsv2ProxyEnableSwitch", "checked", False, timeout_ms=2000)
    gui.wait_for_property("settingsv2ProxySettingsPage", "proxyDraftDirty", False, timeout_ms=2000)
    print("  Back discard leaves persisted settings unchanged: OK")


def wait_for_settings_key(datadir, key, timeout=10.0):
    """Poll regtest/settings.json until `key` is present, up to timeout seconds."""
    settings_path = os.path.join(datadir, "regtest", "settings.json")
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if os.path.exists(settings_path):
            try:
                with open(settings_path, encoding="utf-8") as f:
                    data = json.load(f)
                if key in data:
                    return data
            except (json.JSONDecodeError, OSError):
                pass
        time.sleep(0.1)
    raise AssertionError(
        f"Timed out after {timeout}s waiting for '{key}' in {settings_path}"
    )


def test_proxy_settings_persist(datadir, settings):
    print("\n── test_proxy_settings_persist ─────────────────────────────────────")

    assert settings.get("proxy") == "10.0.0.1:9050", (
        f"Expected proxy='10.0.0.1:9050' in settings.json, got: {settings}"
    )
    print(f"  settings.json proxy entry '{settings['proxy']}': OK")

    assert settings.get("onion") == "127.0.0.1:9150", (
        f"Expected onion='127.0.0.1:9150' in settings.json, got: {settings}"
    )
    print(f"  settings.json onion entry '{settings['onion']}': OK")


def run_tests():
    args = parse_args()
    harness = QmlTestHarness(
        socket_path=args.socket_path,
        reset_settings=False,
    )
    gui = None
    try:
        harness.start()
        gui = harness.driver
        if not args.socket_path and gui.object_exists("onboardingCover"):
            complete_preinit_onboarding(gui)
            gui = harness.wait_for_main_window_reconnect()

        navigate_to_proxy_settings(gui)

        test_default_proxy_toggle(gui)
        test_proxy_valid_address(gui)
        test_proxy_invalid_address(gui)
        test_tor_proxy_toggle(gui)
        test_back_discards_proxy_draft(gui)

        # Prepare state for the persistence test. Runtime proxy settings remain
        # local drafts until the page-level Done button is pressed.
        if harness.datadir:
            if not gui.get_property("settingsv2ProxyEnableSwitch", "checked"):
                gui.click("settingsv2ProxyEnableSwitch")
                gui.wait_for_property("settingsv2ProxyEnableSwitch", "checked", True, timeout_ms=2000)
            gui.wait_for_property("settingsv2ProxyAddressInput", "enabled", True, timeout_ms=2000)
            gui.set_text("settingsv2ProxyAddressInput", "10.0.0.1:9050")
            gui.wait_for_property("settingsv2ProxySettingsPage", "draftProxyValidationError", "", timeout_ms=2000)

            if not gui.get_property("settingsv2TorEnableSwitch", "checked"):
                gui.click("settingsv2TorEnableSwitch")
                gui.wait_for_property("settingsv2TorEnableSwitch", "checked", True, timeout_ms=2000)
            gui.wait_for_property("settingsv2TorAddressInput", "enabled", True, timeout_ms=2000)
            gui.set_text("settingsv2TorAddressInput", "127.0.0.1:9150")
            gui.wait_for_property("settingsv2ProxySettingsPage", "draftTorValidationError", "", timeout_ms=2000)

        leave_proxy_settings_with_done(gui)
        navigate_back_from_connection_settings(gui)

        if harness.datadir:
            settings = wait_for_settings_key(harness.datadir, "proxy")
            test_proxy_settings_persist(harness.datadir, settings)
        else:
            print("\n── test_proxy_settings_persist: skipped (external harness) ──")

        print("\n" + "=" * 50)
        print("All tests PASSED")
        print("=" * 50)

    except Exception as e:
        print(f"\nFAILED: {e}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        if gui is not None:
            dump_qml_tree(gui)
        sys.exit(1)
    finally:
        harness.stop()


if __name__ == '__main__':
    run_tests()
