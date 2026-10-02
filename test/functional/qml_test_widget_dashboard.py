#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Verify widget layout persistence across actual app exits, including a crash."""

import os

from qml_test_harness import QmlTestHarness


def prepare(harness):
    harness.start()
    gui = harness.driver
    gui.wait_for_page("nodeOverview", timeout_ms=10000)
    gui.click("widgetsTabButton")
    gui.wait_for_page("widgetDashboard", timeout_ms=10000)
    gui.set_property("appWindow", "width", 1400)
    gui.set_property("appWindow", "height", 800)
    gui.settle()
    gui.wait_for_property("widgetGrid", "columns", 6)
    return gui


def choose_action(gui, name):
    gui.click("widgetActionsButton")
    gui.wait_for_property("widgetActionsMenu", "opened", True)
    gui.click(name)
    gui.wait_for_property("widgetActionsMenu", "visible", False)


def geometry(gui):
    return tuple(gui.get_property("widget_blockclock", name)
                 for name in ("gridColumn", "gridRow", "columnSpan", "rowSpan"))


def screenshot(gui, name):
    directory = os.getenv("WIDGET_TEST_SCREENSHOT_DIR")
    if directory:
        gui.save_screenshot(os.path.join(directory, name))


def run_tests():
    first = QmlTestHarness(extra_args=["-disablewallet"])
    reopened = None
    empty = None
    copies = None
    try:
        gui = prepare(first)
        assert geometry(gui) == (0, 0, 3, 3)
        screenshot(gui, "dashboard.png")
        choose_action(gui, "editWidgetsButton")
        # Pointer delivery and snapping are covered by Qt Quick tests. These
        # calls exercise the same commit path using the production model.
        gui.invoke("widgetGrid", "resizeTo", ["blockclock", 2, 2])
        gui.invoke("widgetGrid", "resizeTo", ["blockclock", 3, 3])
        gui.invoke("widgetGrid", "moveBy", ["blockclock", 1, 0])
        expected = geometry(gui)
        assert expected == (1, 0, 3, 3), expected
        screenshot(gui, "dashboard-edit.png")

        # Do not click Done and do not allow shutdown handlers to save state.
        first.process.kill()
        first.process.wait(timeout=10)
        first.stop(cleanup=False)
        reopened = QmlTestHarness(extra_args=["-disablewallet"], datadir=first.datadir)
        gui = prepare(reopened)
        assert geometry(gui) == expected
        assert gui.get_property("widgetDashboard", "editing") is False

        gui.set_property("appWindow", "width", 800)
        gui.set_property("appWindow", "height", 1550)
        gui.settle()
        assert gui.get_property("widgetGrid", "columns") == 3
        assert gui.get_property("widgetGrid", "rows") == 6
        column, row, columns, rows = geometry(gui)
        assert 0 <= column <= 3 - columns and 0 <= row <= 6 - rows
        choose_action(gui, "editWidgetsButton")
        screenshot(gui, "dashboard-portrait.png")
        choose_action(gui, "editWidgetsButton")
        # Select by the usable dashboard area, clear of the hysteresis bands.
        choose_action(gui, "editWidgetsButton")
        for width, height, columns, rows in [(900, 1300, 4, 5), (1100, 950, 5, 4), (1400, 800, 6, 3)]:
            gui.set_property("appWindow", "width", width)
            gui.set_property("appWindow", "height", height)
            gui.settle()
            actual_columns = gui.get_property("widgetGrid", "columns")
            assert actual_columns == columns, (width, height, actual_columns, columns)
            assert gui.get_property("widgetGrid", "rows") == rows
            screenshot(gui, f"dashboard-{columns}x{rows}.png")
        assert geometry(gui) == expected
        choose_action(gui, "editWidgetsButton")

        choose_action(gui, "addWidgetButton")
        gui.wait_for_property("widgetPicker", "opened", True)
        assert gui.get_property("widgetPickerRow_blockclock", "enabled") is True
        screenshot(gui, "widget-picker.png")
        gui.invoke("widgetPicker", "close")
        gui.click("widgetRemove_blockclock")
        assert not gui.object_exists("widget_blockclock")
        reopened.stop(cleanup=False)

        empty = QmlTestHarness(extra_args=["-disablewallet"], datadir=first.datadir)
        gui = prepare(empty)
        assert not gui.object_exists("widget_blockclock")
        screenshot(gui, "dashboard-empty.png")
        choose_action(gui, "addWidgetButton")
        gui.wait_for_property("widgetPicker", "opened", True)
        assert gui.get_property("widgetPickerRow_blockclock", "enabled") is True
        gui.click("widgetPickerRow_blockclock")
        gui.click("widgetPickerSize_blockclock_3x3")
        gui.wait_for_object("widget_blockclock")
        assert geometry(gui) == (0, 0, 3, 3)
        choose_action(gui, "addWidgetButton")
        gui.wait_for_property("widgetPicker", "opened", True)
        gui.click("widgetPickerSize_blockclock_2x2")
        gui.wait_for_property("widgetPicker", "visible", False)
        frames = [obj["objectName"] for obj in gui.list_objects()
                  if obj["objectName"].startswith("widget_")]
        assert len(frames) == 2, frames
        duplicate = next(name for name in frames if name != "widget_blockclock")
        assert gui.get_property(duplicate, "widgetId") == "blockclock"
        assert gui.get_property(duplicate, "columnSpan") == 2
        assert geometry(gui)[2:] == (3, 3)
        screenshot(gui, "dashboard-duplicate-sizes.png")
        empty.stop(cleanup=False)

        copies = QmlTestHarness(extra_args=["-disablewallet"], datadir=first.datadir)
        gui = prepare(copies)
        gui.wait_for_object(duplicate)
        assert gui.get_property(duplicate, "columnSpan") == 2
        assert geometry(gui)[2:] == (3, 3)
        choose_action(gui, "editWidgetsButton")
        gui.click("widgetRemove_blockclock")
        assert not gui.object_exists("widget_blockclock")
        assert gui.object_exists(duplicate)
        assert gui.get_property(duplicate, "widgetId") == "blockclock"
        screenshot(gui, "dashboard-duplicate-after-remove.png")
        print("Widget dashboard persistence and duplicates test PASSED")
    finally:
        if copies:
            copies.stop(cleanup=False)
        if empty:
            empty.stop(cleanup=False)
        if reopened:
            reopened.stop(cleanup=False)
        first.stop()


if __name__ == "__main__":
    run_tests()
