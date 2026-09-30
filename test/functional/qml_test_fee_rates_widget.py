#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Exercise the fee widget without a wallet, including restart persistence."""

from qml_test_harness import QmlTestHarness
from qml_test_widget_dashboard import prepare, screenshot, choose_action


def geometry(gui):
    return tuple(gui.get_property("widget_fee-rates", name)
                 for name in ("gridColumn", "gridRow", "columnSpan", "rowSpan"))


def run_tests():
    first = QmlTestHarness(extra_args=["-disablewallet"])
    reopened = None
    removed = None
    try:
        gui = prepare(first)
        choose_action(gui, "addWidgetButton")
        gui.wait_for_property("widgetPicker", "opened", True)
        gui.click("widgetPickerRow_fee-rates")
        gui.click("widgetPickerSize_fee-rates_2x1")
        gui.wait_for_object("feeRatesWidget")
        assert geometry(gui)[2:] == (2, 1)
        assert gui.get_property("widget_fee-rates", "supportedSizes") == [
            {"columns": 2, "rows": 1, "label": "Medium"}, {"columns": 3, "rows": 2, "label": "Large"},
            {"columns": 1, "rows": 1, "label": "Small"}]
        choose_action(gui, "editWidgetsButton")
        gui.wait_for_property("feeRatesStatus", "text", "Estimates unavailable")
        assert gui.get_property("feeRatesWidget", "rates") == [-1, -1, -1, -1]
        screenshot(gui, "fee-rates-compact.png")
        gui.invoke("widgetGrid", "resizeTo", ["fee-rates", 3, 2])
        assert geometry(gui)[2:] == (3, 2)
        expected = geometry(gui)
        screenshot(gui, "fee-rates-large.png")
        gui.set_property("appWindow", "width", 800)
        gui.set_property("appWindow", "height", 1100)
        gui.settle()
        assert geometry(gui)[2:] == (3, 2)
        screenshot(gui, "fee-rates-portrait.png")
        gui.set_property("appWindow", "width", 1400)
        gui.set_property("appWindow", "height", 800)
        gui.settle()
        assert geometry(gui) == expected, (geometry(gui), expected, gui.get_property("widgetGrid", "columns"))
        choose_action(gui, "editWidgetsButton")
        screenshot(gui, "fee-rates-edit.png")
        first.stop(cleanup=False)

        reopened = QmlTestHarness(extra_args=["-disablewallet"], datadir=first.datadir)
        gui = prepare(reopened)
        gui.wait_for_object("feeRatesWidget")
        assert geometry(gui) == expected, (geometry(gui), expected, gui.get_property("widgetGrid", "columns"))
        choose_action(gui, "addWidgetButton")
        gui.wait_for_property("widgetPicker", "opened", True)
        assert gui.get_property("widgetPickerRow_fee-rates", "enabled") is True
        gui.invoke("widgetPicker", "close")
        gui.click("widgetRemove_fee-rates")
        assert not gui.object_exists("feeRatesWidget")
        assert gui.object_exists("widget_blockclock")
        reopened.stop(cleanup=False)

        removed = QmlTestHarness(extra_args=["-disablewallet"], datadir=first.datadir)
        gui = prepare(removed)
        assert not gui.object_exists("feeRatesWidget")
        print("Fee rates widget functional test PASSED")
    finally:
        if removed:
            removed.stop(cleanup=False)
        if reopened:
            reopened.stop(cleanup=False)
        first.stop()


if __name__ == "__main__":
    run_tests()
