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

import argparse
from datetime import datetime
import os
import re
import sys

from qml_test_harness import (
    QmlTestHarness,
    complete_onboarding,
    dump_qml_tree,
)


POST_ONBOARDING_TIMEOUT_MS = 30000
SETTINGS_TIMEOUT_MS = 10000


def parse_args():
    parser = argparse.ArgumentParser(
        description="Disablewallet node-only GUI functional test",
        add_help=True,
    )
    parser.add_argument(
        "--socket-path",
        help="Connect to an already-running bitcoin-core-app instance at "
             "this Unix socket path instead of launching a new one.",
    )
    parser.add_argument(
        "--save-screenshots",
        action="store_true",
        help="Save a PNG at each GUI checkpoint under test/artifacts/",
    )
    return parser.parse_args()


def make_screenshot_root():
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
    artifacts_root = os.path.join(repo_root, "test", "artifacts")
    timestamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    screenshot_root = os.path.join(artifacts_root, f"qml_test_disablewallet_boot-{timestamp}")
    os.makedirs(screenshot_root, exist_ok=True)
    return screenshot_root


class CheckpointRecorder:
    def __init__(self, save_screenshots, screenshot_root):
        self.save_screenshots = save_screenshots
        self.screenshot_root = screenshot_root
        self.index = 0

    def _sanitize_label(self, label):
        return re.sub(r"[^a-z0-9]+", "-", label.lower()).strip("-") or "checkpoint"

    def checkpoint(self, label, gui=None):
        self.index += 1
        prefix = f"[qml_test_disablewallet_boot] checkpoint {self.index:02d}"
        print(f"{prefix}: {label}")
        if gui is None:
            return

        gui.settle()

        if not self.save_screenshots:
            return

        filename = f"{self.index:02d}-{self._sanitize_label(label)}.png"
        screenshot_path = os.path.join(self.screenshot_root, filename)
        screenshot = gui.save_screenshot(screenshot_path)
        print(
            f"{prefix}: screenshot saved to {screenshot['path']} "
            f"({screenshot['width']}x{screenshot['height']})"
        )


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
    for context_property in ("walletController", "walletListModel"):
        value = gui.get_context_property(context_property)
        assert value["exists"] is False, (
            f"{context_property} should not be exposed in -disablewallet mode: "
            f"{value}"
        )

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


def walk_about_settings(gui, checkpoints):
    print("  Opening About settings")
    gui.click("gotoAboutSetting")
    gui.wait_for_page("settingsAbout", timeout_ms=SETTINGS_TIMEOUT_MS)
    wait_for_node_settings_idle(gui)
    checkpoints.checkpoint("about settings opened", gui)

    gui.click("gotoDeveloperSetting")
    gui.wait_for_page("settingsDeveloper", timeout_ms=SETTINGS_TIMEOUT_MS)
    wait_for_node_settings_idle(gui)
    checkpoints.checkpoint("developer settings opened", gui)
    gui.click("settingsDeveloperBack")
    gui.wait_for_page("settingsAbout", timeout_ms=SETTINGS_TIMEOUT_MS)
    wait_for_node_settings_idle(gui)

    back_to_node_settings_root(gui, "settingsAboutBack")
    checkpoints.checkpoint("returned from about settings", gui)


def walk_display_settings(gui, checkpoints):
    print("  Opening Display settings")
    gui.click("gotoDisplay")
    gui.wait_for_page("gotoDisplayUnit", timeout_ms=SETTINGS_TIMEOUT_MS)
    wait_for_node_settings_idle(gui)
    checkpoints.checkpoint("display settings opened", gui)

    gui.click("gotoDisplayUnit")
    gui.wait_for_page("settingsDisplayUnitPage", timeout_ms=SETTINGS_TIMEOUT_MS)
    wait_for_node_settings_idle(gui)
    checkpoints.checkpoint("display unit settings opened", gui)
    gui.click("settingsDisplayUnitBack")
    gui.wait_for_page("gotoDisplayUnit", timeout_ms=SETTINGS_TIMEOUT_MS)
    wait_for_node_settings_idle(gui)

    gui.click("gotoLanguage")
    gui.wait_for_page("settingsLanguagePage", timeout_ms=SETTINGS_TIMEOUT_MS)
    wait_for_node_settings_idle(gui)
    checkpoints.checkpoint("language settings opened", gui)
    gui.click("settingsLanguageBack")
    gui.wait_for_page("gotoLanguage", timeout_ms=SETTINGS_TIMEOUT_MS)
    wait_for_node_settings_idle(gui)

    back_to_node_settings_root(gui, "settingsDisplayBack")
    checkpoints.checkpoint("returned from display settings", gui)


def walk_storage_settings(gui, checkpoints):
    print("  Opening Storage settings")
    gui.click("gotoStorage")
    gui.wait_for_page("settingsStorageBack", timeout_ms=SETTINGS_TIMEOUT_MS)
    wait_for_node_settings_idle(gui)
    checkpoints.checkpoint("storage settings opened", gui)
    back_to_node_settings_root(gui, "settingsStorageBack")
    checkpoints.checkpoint("returned from storage settings", gui)


def walk_connection_settings(gui, checkpoints):
    print("  Opening Connection settings")
    gui.click("settingsConnection")
    gui.wait_for_page("gotoProxy", timeout_ms=SETTINGS_TIMEOUT_MS)
    wait_for_node_settings_idle(gui)
    checkpoints.checkpoint("connection settings opened", gui)

    gui.click("gotoProxy")
    gui.wait_for_page("settingsProxy", timeout_ms=SETTINGS_TIMEOUT_MS)
    wait_for_node_settings_idle(gui)
    checkpoints.checkpoint("proxy settings opened", gui)
    gui.click("settingsProxyBack")
    gui.wait_for_page("gotoProxy", timeout_ms=SETTINGS_TIMEOUT_MS)
    wait_for_node_settings_idle(gui)

    back_to_node_settings_root(gui, "settingsConnectionBack")
    checkpoints.checkpoint("returned from connection settings", gui)


def walk_peers_settings(gui, checkpoints):
    print("  Opening Peers settings")
    gui.click("settingsPeers")
    gui.wait_for_page("peers", timeout_ms=SETTINGS_TIMEOUT_MS)
    wait_for_node_settings_idle(gui)
    checkpoints.checkpoint("peers settings opened", gui)
    back_to_node_settings_root(gui, "peersBackButton")
    checkpoints.checkpoint("returned from peers settings", gui)


def walk_network_traffic_settings(gui, checkpoints):
    print("  Opening Network Traffic settings")
    gui.click("settingsNetworkTraffic")
    gui.wait_for_page("networkTrafficBackButton", timeout_ms=SETTINGS_TIMEOUT_MS)
    wait_for_node_settings_idle(gui)
    checkpoints.checkpoint("network traffic settings opened", gui)
    back_to_node_settings_root(gui, "networkTrafficBackButton")
    checkpoints.checkpoint("returned from network traffic settings", gui)


def walk_debug_log_settings(gui, checkpoints):
    print("  Opening Debug Log settings")
    gui.click("settingsDebugLog")
    gui.wait_for_page("debugLogSearchField", timeout_ms=SETTINGS_TIMEOUT_MS)
    wait_for_node_settings_idle(gui)
    checkpoints.checkpoint("debug log settings opened", gui)
    back_to_node_settings_root(gui, "debugLogBackButton")
    checkpoints.checkpoint("returned from debug log settings", gui)


def run_tests():
    args = parse_args()
    screenshot_root = make_screenshot_root() if args.save_screenshots else None
    checkpoints = CheckpointRecorder(args.save_screenshots, screenshot_root)
    harness = QmlTestHarness(socket_path=args.socket_path, extra_args=["-disablewallet"])
    try:
        harness.start()
        gui = harness.driver
        checkpoints.checkpoint("GUI launched", gui)

        complete_onboarding(gui)
        gui.wait_for_page("nodeSettingsButton", timeout_ms=POST_ONBOARDING_TIMEOUT_MS)

        current_page = gui.get_current_page()
        assert current_page == "nodeRunner", (
            f"Expected -disablewallet onboarding to exit to nodeRunner, got {current_page!r}"
        )
        print("Reached node-only main screen")
        checkpoints.checkpoint("node-only main screen reached", gui)

        open_node_settings(gui)
        assert_wallet_ui_absent(gui)
        checkpoints.checkpoint("node settings opened without wallet UI", gui)

        walk_about_settings(gui, checkpoints)
        walk_display_settings(gui, checkpoints)
        walk_storage_settings(gui, checkpoints)
        walk_connection_settings(gui, checkpoints)
        walk_peers_settings(gui, checkpoints)
        walk_network_traffic_settings(gui, checkpoints)
        walk_debug_log_settings(gui, checkpoints)

        gui.click("nodeSettingsDoneButton")
        gui.wait_for_page("nodeSettingsButton", timeout_ms=SETTINGS_TIMEOUT_MS)
        checkpoints.checkpoint("node settings closed", gui)

    except Exception as e:
        print(f"\nFAILED: {e}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        if harness.driver:
            checkpoints.checkpoint("failure state", harness.driver)
            dump_qml_tree(harness.driver)
        sys.exit(1)
    finally:
        harness.stop()

    print("\n" + "=" * 60)
    print("Disablewallet node-only boot test PASSED")
    print("=" * 60)


if __name__ == "__main__":
    run_tests()
