#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""End-to-end tests for the redesigned Display settings page."""

import shutil
import sys

from qml_test_harness import (
    QmlTestHarness,
    complete_onboarding,
    dump_qml_tree,
    parse_args,
)
from qml_driver import QmlDriverError


POST_ONBOARDING_TIMEOUT_MS = 30_000
DISPLAY_SETTING_ROWS = (
    "displayThemeRow",
    "displayBlockStatusSizeRow",
    "displayMoneyFontRow",
    "displayUnitRow",
    "displayLanguageRow",
    "displayTransactionUrlsRow",
)


def navigate_to_display_settings(gui):
    """Open Settings and select Display from the sidebar."""
    gui.click("nodeSettingsButton")
    gui.wait_for_property("settingsSidebar_display", "visible", True, timeout_ms=5000)
    gui.click("settingsSidebar_display")
    gui.wait_for_page("displaySettingsPage", timeout_ms=5000)
    gui.wait_for_page("displayUnitPicker", timeout_ms=5000)
    print("  Navigated to redesigned Display settings page")


def assert_display_rows_have_no_descriptions(gui):
    for row in DISPLAY_SETTING_ROWS:
        description = gui.get_property(row, "description")
        assert description == "", f"{row} should not show subtext, got: {description!r}"


def select_display_unit(gui, item_name, expected_text):
    gui.click("displayUnitPickerButton")
    gui.wait_for_property(item_name, "visible", True, timeout_ms=3000)
    gui.click(item_name)
    gui.wait_for_property(
        "displayUnitPicker", "currentText", expected_text, timeout_ms=3000
    )


def select_language(gui, search_text, item_name):
    gui.click("displayLanguageRow")
    gui.wait_for_page("settingsLanguagePage", timeout_ms=5000)
    if search_text:
        gui.set_text("languageSearch", search_text)
    gui.wait_for_page(item_name, timeout_ms=3000)
    gui.click(item_name)
    gui.wait_for_page("displayLanguageRow", timeout_ms=5000)


def reset_display_unit_to_btc(gui):
    select_display_unit(gui, "displayUnitBTC", "BTC")


def reset_language_to_system_default(gui):
    select_language(gui, "", "language_")


def test_display_unit_selection(gui):
    print("\n── test_display_unit_selection ───────────────────────────────")
    try:
        reset_display_unit_to_btc(gui)
        select_display_unit(gui, "displayUnitSAT", "sat")
        assert gui.get_property("displayUnitPicker", "currentValue") == 3
        print("  Display unit changed from BTC to sat  PASSED")
    finally:
        try:
            reset_display_unit_to_btc(gui)
        except QmlDriverError:
            pass


def test_language_selection(gui):
    print("\n── test_language_selection ───────────────────────────────────")
    try:
        select_language(gui, "español", "language_es")
        assert_display_rows_have_no_descriptions(gui)

        language_title = gui.get_property("displayLanguageRow", "title")
        unit_title = gui.get_property("displayUnitRow", "title")
        assert language_title == "Idioma", language_title
        assert unit_title == "Unidad de visualización", unit_title
        print("  Spanish translated the inline Display rows  PASSED")
    finally:
        try:
            reset_language_to_system_default(gui)
        except QmlDriverError:
            pass


def test_settings_persistence(datadir):
    print("\n── test_settings_persistence ─────────────────────────────────")
    harness = QmlTestHarness(
        extra_args=["-disablewallet"],
        reset_settings=False,
        datadir=datadir,
    )
    try:
        harness.start()
        gui = harness.driver
        gui.wait_for_page("nodeSettingsButton", timeout_ms=POST_ONBOARDING_TIMEOUT_MS)
        navigate_to_display_settings(gui)

        assert gui.get_property("displayUnitPicker", "currentValue") == 3
        assert gui.get_property("displayLanguageRow", "title") == "Idioma"
        assert gui.get_property("displayUnitRow", "title") == "Unidad de visualización"
        print("  Display unit and language persisted across restart  PASSED")

        reset_display_unit_to_btc(gui)
        reset_language_to_system_default(gui)
    finally:
        harness.stop()


def run_tests():
    args = parse_args()
    harness = QmlTestHarness(socket_path=args.socket_path, extra_args=["-disablewallet"])
    datadir = None
    tmpdir = None
    try:
        harness.start()
        gui = harness.driver
        complete_onboarding(gui)
        gui.wait_for_page("nodeSettingsButton", timeout_ms=POST_ONBOARDING_TIMEOUT_MS)

        navigate_to_display_settings(gui)
        assert_display_rows_have_no_descriptions(gui)
        test_display_unit_selection(gui)
        test_language_selection(gui)

        select_display_unit(gui, "displayUnitSAT", "sat")
        select_language(gui, "español", "language_es")
        datadir = harness.datadir
        tmpdir = harness.tmpdir
    except Exception as error:
        print(f"\nFAILED: {error}", file=sys.stderr)
        if harness.driver:
            dump_qml_tree(harness.driver)
        raise
    finally:
        harness.stop(cleanup=False)

    try:
        test_settings_persistence(datadir)
    finally:
        if tmpdir:
            shutil.rmtree(tmpdir, ignore_errors=True)

    print("\nAll display settings tests PASSED")


if __name__ == "__main__":
    try:
        run_tests()
    except Exception:
        sys.exit(1)
