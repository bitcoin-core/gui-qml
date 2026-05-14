#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Smoke test for runtime -disablewallet node-only mode.

This test walks onboarding with wallet support disabled at runtime and verifies
that the app lands on the node screen, hides wallet settings, and can open each
node settings page.

This test requires:
  - bitcoin-core-app built with -DENABLE_TEST_AUTOMATION=ON
"""

import sys

from qml_test_harness import (
    QmlTestHarness,
    complete_onboarding,
    dump_qml_tree,
    parse_args,
)


POST_ONBOARDING_TIMEOUT_MS = 30000
SETTINGS_TIMEOUT_MS = 10000


def wait_for_node_settings_idle(gui):
    gui.wait_for_property(
        "nodeSettingsStack",
        "busy",
        False,
        timeout_ms=SETTINGS_TIMEOUT_MS,
    )


def wait_for_node_settings_root(gui):
    gui.wait_for_page("gotoAboutSetting", timeout_ms=SETTINGS_TIMEOUT_MS)
    wait_for_node_settings_idle(gui)


def back_to_node_settings_root(gui, back_button):
    gui.click(back_button)
    wait_for_node_settings_root(gui)


def open_node_settings(gui):
    gui.click("nodeSettingsButton")
    wait_for_node_settings_root(gui)


def assert_wallet_ui_absent(gui):
    objects = {entry["objectName"] for entry in gui.list_objects()}
    unexpected = {
        "createWalletWizard",
        "createWalletIntroPage",
        "walletBadge",
        "desktopWalletsActivityTab",
        "desktopWalletSettingsTabButton",
    } & objects
    assert not unexpected, (
        "Wallet UI should not be instantiated in -disablewallet mode: "
        f"{sorted(unexpected)}"
    )

    wallet_settings_visible = gui.get_property("settingsWallet", "visible")
    assert wallet_settings_visible is False, (
        "External Signer settings row should be hidden in -disablewallet mode, "
        f"got {wallet_settings_visible!r}"
    )


def walk_about_settings(gui):
    print("  Opening About settings")
    gui.click("gotoAboutSetting")
    gui.wait_for_page("settingsAbout", timeout_ms=SETTINGS_TIMEOUT_MS)
    wait_for_node_settings_idle(gui)

    gui.click("gotoDeveloperSetting")
    gui.wait_for_page("settingsDeveloper", timeout_ms=SETTINGS_TIMEOUT_MS)
    wait_for_node_settings_idle(gui)
    gui.click("settingsDeveloperBack")
    gui.wait_for_page("settingsAbout", timeout_ms=SETTINGS_TIMEOUT_MS)
    wait_for_node_settings_idle(gui)

    back_to_node_settings_root(gui, "settingsAboutBack")


def walk_display_settings(gui):
    print("  Opening Display settings")
    gui.click("gotoDisplay")
    gui.wait_for_page("gotoDisplayUnit", timeout_ms=SETTINGS_TIMEOUT_MS)
    wait_for_node_settings_idle(gui)

    gui.click("gotoDisplayUnit")
    gui.wait_for_page("settingsDisplayUnitPage", timeout_ms=SETTINGS_TIMEOUT_MS)
    wait_for_node_settings_idle(gui)
    gui.click("settingsDisplayUnitBack")
    gui.wait_for_page("gotoDisplayUnit", timeout_ms=SETTINGS_TIMEOUT_MS)
    wait_for_node_settings_idle(gui)

    gui.click("gotoLanguage")
    gui.wait_for_page("settingsLanguagePage", timeout_ms=SETTINGS_TIMEOUT_MS)
    wait_for_node_settings_idle(gui)
    gui.click("settingsLanguageBack")
    gui.wait_for_page("gotoLanguage", timeout_ms=SETTINGS_TIMEOUT_MS)
    wait_for_node_settings_idle(gui)

    back_to_node_settings_root(gui, "settingsDisplayBack")


def walk_storage_settings(gui):
    print("  Opening Storage settings")
    gui.click("gotoStorage")
    gui.wait_for_page("settingsStorageBack", timeout_ms=SETTINGS_TIMEOUT_MS)
    wait_for_node_settings_idle(gui)
    back_to_node_settings_root(gui, "settingsStorageBack")


def walk_connection_settings(gui):
    print("  Opening Connection settings")
    gui.click("settingsConnection")
    gui.wait_for_page("gotoProxy", timeout_ms=SETTINGS_TIMEOUT_MS)
    wait_for_node_settings_idle(gui)

    gui.click("gotoProxy")
    gui.wait_for_page("settingsProxy", timeout_ms=SETTINGS_TIMEOUT_MS)
    wait_for_node_settings_idle(gui)
    gui.click("settingsProxyBack")
    gui.wait_for_page("gotoProxy", timeout_ms=SETTINGS_TIMEOUT_MS)
    wait_for_node_settings_idle(gui)

    back_to_node_settings_root(gui, "settingsConnectionBack")


def walk_peers_settings(gui):
    print("  Opening Peers settings")
    gui.click("settingsPeers")
    gui.wait_for_page("peers", timeout_ms=SETTINGS_TIMEOUT_MS)
    wait_for_node_settings_idle(gui)
    back_to_node_settings_root(gui, "peersBackButton")


def walk_network_traffic_settings(gui):
    print("  Opening Network Traffic settings")
    gui.click("settingsNetworkTraffic")
    gui.wait_for_page("networkTrafficBackButton", timeout_ms=SETTINGS_TIMEOUT_MS)
    wait_for_node_settings_idle(gui)
    back_to_node_settings_root(gui, "networkTrafficBackButton")


def walk_debug_log_settings(gui):
    print("  Opening Debug Log settings")
    gui.click("settingsDebugLog")
    gui.wait_for_page("debugLogSearchField", timeout_ms=SETTINGS_TIMEOUT_MS)
    wait_for_node_settings_idle(gui)
    back_to_node_settings_root(gui, "debugLogBackButton")


def run_tests():
    args = parse_args()
    harness = QmlTestHarness(socket_path=args.socket_path, extra_args=["-disablewallet"])
    try:
        harness.start()
        gui = harness.driver

        complete_onboarding(gui)
        gui.wait_for_page("nodeSettingsButton", timeout_ms=POST_ONBOARDING_TIMEOUT_MS)

        current_page = gui.get_current_page()
        assert current_page == "nodeRunner", (
            f"Expected -disablewallet onboarding to exit to nodeRunner, got {current_page!r}"
        )
        print("Reached node-only main screen")

        open_node_settings(gui)
        assert_wallet_ui_absent(gui)

        walk_about_settings(gui)
        walk_display_settings(gui)
        walk_storage_settings(gui)
        walk_connection_settings(gui)
        walk_peers_settings(gui)
        walk_network_traffic_settings(gui)
        walk_debug_log_settings(gui)

        gui.click("nodeSettingsDoneButton")
        gui.wait_for_page("nodeSettingsButton", timeout_ms=SETTINGS_TIMEOUT_MS)

    except Exception as e:
        print(f"\nFAILED: {e}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        if harness.driver:
            dump_qml_tree(harness.driver)
        sys.exit(1)
    finally:
        harness.stop()

    print("\n" + "=" * 60)
    print("Disablewallet node-only boot test PASSED")
    print("=" * 60)


if __name__ == "__main__":
    run_tests()
