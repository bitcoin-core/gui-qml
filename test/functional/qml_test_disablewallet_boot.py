#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Smoke test for runtime -disablewallet node-only mode.

This test launches with wallet support disabled at runtime and verifies that the
app lands directly on the node screen, hides wallet settings, and can open each
node settings page.

This test requires:
  - bitcoin-core-app built with -DENABLE_TEST_AUTOMATION=ON
"""

import argparse
from datetime import datetime
import json
import os
import re
import sys

from qml_test_harness import (
    QmlTestHarness,
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


def open_node_settings(gui):
    gui.click("nodeSettingsButton")
    gui.wait_for_page("settingsSidebar_about", timeout_ms=SETTINGS_TIMEOUT_MS)


def prepend_config_line(datadir, line):
    conf_path = os.path.join(datadir, "bitcoin.conf")
    with open(conf_path, encoding="utf8") as conf:
        existing_config = conf.read()
    with open(conf_path, "w", encoding="utf8") as conf:
        conf.write(f"{line}\n")
        conf.write(existing_config)


def write_settings_json(datadir, settings):
    settings_dir = os.path.join(datadir, "regtest")
    os.makedirs(settings_dir, exist_ok=True)
    with open(os.path.join(settings_dir, "settings.json"), "w", encoding="utf8") as settings_file:
        json.dump(settings, settings_file)


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

    assert not gui.object_exists("settingsSidebar_wallet"), (
        "Wallet settings row should not be instantiated in -disablewallet mode"
    )


def assert_node_only_boot(gui):
    gui.wait_for_page("nodeSettingsButton", timeout_ms=POST_ONBOARDING_TIMEOUT_MS)

    current_page = gui.get_current_page()
    assert current_page == "nodeRunner", (
        f"Expected -disablewallet startup to show nodeRunner, got {current_page!r}"
    )
    assert gui.object_exists("nodeRunner"), "Expected nodeRunner in node-only mode"
    assert not gui.object_exists("walletBadge"), "Did not expect walletBadge in node-only mode"


def assert_wallet_boot(gui):
    gui.wait_for_object("mainPageStack", timeout_ms=POST_ONBOARDING_TIMEOUT_MS)
    gui.wait_for_object("walletBadge", timeout_ms=POST_ONBOARDING_TIMEOUT_MS)
    current_page = gui.get_current_page()
    assert current_page == "desktopWalletsPage", (
        f"Expected wallet-enabled startup to show desktopWalletsPage, got {current_page!r}"
    )
    assert gui.get_context_property("walletController")["exists"] is True
    assert gui.get_context_property("walletListModel")["exists"] is True
    assert gui.object_exists("walletBadge"), "Expected walletBadge in wallet-enabled mode"
    assert not gui.object_exists("nodeRunner"), "Did not expect nodeRunner in wallet-enabled mode"


def walk_about_settings(gui, checkpoints):
    print("  Opening About settings (sidebar)")
    gui.click("settingsSidebar_about")
    gui.wait_for_page("settingsv2AboutSettingsPage", timeout_ms=SETTINGS_TIMEOUT_MS)
    checkpoints.checkpoint("about settings opened", gui)

    gui.click("settingsv2AboutDeveloperRow")
    gui.wait_for_page("settingsDeveloper", timeout_ms=SETTINGS_TIMEOUT_MS)
    checkpoints.checkpoint("developer settings opened", gui)
    gui.click("settingsDeveloperBack")
    gui.wait_for_page("settingsv2AboutSettingsPage", timeout_ms=SETTINGS_TIMEOUT_MS)
    checkpoints.checkpoint("returned from developer settings", gui)


def walk_display_settings(gui, checkpoints):
    print("  Opening Display settings (sidebar)")
    gui.click("settingsSidebar_display")
    gui.wait_for_page("settingsv2DisplayUnitPicker", timeout_ms=SETTINGS_TIMEOUT_MS)
    checkpoints.checkpoint("display settings opened", gui)

    gui.click("settingsv2DisplayUnitPickerButton")
    gui.wait_for_page("settingsv2DisplayUnitPickerMenu", timeout_ms=SETTINGS_TIMEOUT_MS)
    checkpoints.checkpoint("display unit picker opened", gui)
    gui.click("settingsv2DisplayUnitBTC")

    gui.click("settingsv2DisplayLanguageRow")
    gui.wait_for_page("settingsLanguagePage", timeout_ms=SETTINGS_TIMEOUT_MS)
    checkpoints.checkpoint("language settings opened", gui)
    gui.click("settingsLanguageBack")
    gui.wait_for_page("settingsv2DisplayLanguageRow", timeout_ms=SETTINGS_TIMEOUT_MS)
    checkpoints.checkpoint("returned from display sub-pages", gui)


def walk_storage_settings(gui, checkpoints):
    print("  Opening Storage settings (sidebar)")
    gui.click("settingsSidebar_storage")
    gui.wait_for_page("settingsv2StorageSettingsPage", timeout_ms=SETTINGS_TIMEOUT_MS)
    checkpoints.checkpoint("storage settings opened", gui)


def walk_connection_settings(gui, checkpoints):
    print("  Opening Connection settings (sidebar)")
    gui.click("settingsSidebar_connection")
    gui.wait_for_page("settingsv2ProxySettingsRow", timeout_ms=SETTINGS_TIMEOUT_MS)
    checkpoints.checkpoint("connection settings opened", gui)

    gui.click("settingsv2ProxySettingsRow")
    gui.wait_for_page("settingsv2ProxySettingsPage", timeout_ms=SETTINGS_TIMEOUT_MS)
    checkpoints.checkpoint("proxy settings opened", gui)
    gui.click("settingsv2ProxySettingsBackButton")
    gui.wait_for_page("settingsv2ProxySettingsRow", timeout_ms=SETTINGS_TIMEOUT_MS)
    checkpoints.checkpoint("returned from proxy settings", gui)


def walk_peers(gui, checkpoints):
    print("  Opening Peers (header icon)")
    gui.click("peersTabButton")
    gui.wait_for_page("peers", timeout_ms=SETTINGS_TIMEOUT_MS)
    checkpoints.checkpoint("peers opened", gui)
    gui.click("peersBackButton")
    gui.wait_for_page("nodeRunner", timeout_ms=SETTINGS_TIMEOUT_MS)
    checkpoints.checkpoint("returned from peers", gui)


def walk_network_traffic_settings(gui, checkpoints):
    print("  Opening Network Traffic settings (sidebar)")
    gui.click("settingsSidebar_network-traffic")
    gui.wait_for_page("settingsv2NetworkTrafficSettingsPage", timeout_ms=SETTINGS_TIMEOUT_MS)
    checkpoints.checkpoint("network traffic settings opened", gui)


def walk_debug_log_settings(gui, checkpoints):
    print("  Opening Debug Log settings (sidebar)")
    gui.click("settingsSidebar_debug-log")
    gui.wait_for_page("debugLogSearchField", timeout_ms=SETTINGS_TIMEOUT_MS)
    checkpoints.checkpoint("debug log settings opened", gui)


def run_node_only_flow(harness, checkpoints, *, full_walk):
    try:
        harness.start()
        gui = harness.driver
        checkpoints.checkpoint("GUI launched", gui)

        assert_node_only_boot(gui)
        print("Reached node-only main screen")
        checkpoints.checkpoint("node-only main screen reached", gui)

        if full_walk:
            walk_peers(gui, checkpoints)

        open_node_settings(gui)
        assert_wallet_ui_absent(gui)
        checkpoints.checkpoint("node settings opened without wallet UI", gui)

        if full_walk:
            walk_about_settings(gui, checkpoints)
            walk_display_settings(gui, checkpoints)
            walk_storage_settings(gui, checkpoints)
            walk_connection_settings(gui, checkpoints)
            walk_network_traffic_settings(gui, checkpoints)
            walk_debug_log_settings(gui, checkpoints)

        gui.click("settingsv2SettingsDoneButton")
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


def run_cli_disablewallet_flow(args, checkpoints):
    harness = QmlTestHarness(socket_path=args.socket_path, extra_args=["-disablewallet"])
    print("\n-- cli -disablewallet -----------------------------------------")
    run_node_only_flow(harness, checkpoints, full_walk=True)


def run_config_disablewallet_flow(checkpoints):
    harness = QmlTestHarness()
    prepend_config_line(harness.datadir, "disablewallet=1")
    print("\n-- bitcoin.conf disablewallet=1 -------------------------------")
    run_node_only_flow(harness, checkpoints, full_walk=False)


def run_settings_disablewallet_flow(checkpoints):
    harness = QmlTestHarness()
    write_settings_json(harness.datadir, {"disablewallet": True})
    print("\n-- settings.json disablewallet=true ---------------------------")
    run_node_only_flow(harness, checkpoints, full_walk=False)


def run_cli_enable_override_flow(checkpoints):
    harness = QmlTestHarness(extra_args=["-disablewallet=0"])
    prepend_config_line(harness.datadir, "disablewallet=1")
    print("\n-- cli -disablewallet=0 overrides bitcoin.conf ----------------")
    try:
        harness.start()
        gui = harness.driver
        checkpoints.checkpoint("GUI launched with CLI wallet override", gui)
        assert_wallet_boot(gui)
        checkpoints.checkpoint("wallet UI opened after CLI override", gui)
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


def run_tests():
    args = parse_args()
    screenshot_root = make_screenshot_root() if args.save_screenshots else None
    checkpoints = CheckpointRecorder(args.save_screenshots, screenshot_root)

    run_cli_disablewallet_flow(args, checkpoints)
    if not args.socket_path:
        run_config_disablewallet_flow(checkpoints)
        run_settings_disablewallet_flow(checkpoints)
        run_cli_enable_override_flow(checkpoints)

    print("\n" + "=" * 60)
    print("Disablewallet node-only boot test PASSED")
    print("=" * 60)


if __name__ == "__main__":
    run_tests()
