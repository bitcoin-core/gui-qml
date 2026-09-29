#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Exercise local header statistics, epoch rollover, reorg and layout persistence."""

import time

from qml_test_harness import QmlTestHarness
from qml_test_widget_dashboard import prepare, screenshot, choose_action
from qml_wallet_test_lib import RPC_USER, RPC_PASS, rpc_call, wait_for_rpc


def run_tests():
    args = ["-disablewallet", f"-rpcuser={RPC_USER}", f"-rpcpassword={RPC_PASS}"]
    first = QmlTestHarness(extra_args=args)
    reopened = None
    try:
        gui = prepare(first)
        port = first.rpc_port
        wait_for_rpc(port)
        choose_action(gui, "addWidgetButton")
        gui.wait_for_property("widgetPicker", "opened", True)
        gui.click("widgetPickerAdd_difficulty-period")
        gui.wait_for_object("difficultyPeriodWidget")
        assert gui.get_property("widget_difficulty-period", "supportedSizes") == [
            {"columns": 2, "rows": 1, "label": "Medium"}, {"columns": 3, "rows": 2, "label": "Large"},
            {"columns": 1, "rows": 1, "label": "Small"}]
        choose_action(gui, "editWidgetsButton")
        # This Core version uses a one-day (144-block) interval on regtest.
        rpc_call(port, "generatetodescriptor", [90, "raw(51)"])
        gui.wait_for_property("difficultyProgress", "text", "62%", timeout_ms=40000)
        assert gui.get_property("difficultyBlocksLeft", "text") == "54 blocks left"
        # Regtest never retargets; there is no previous epoch yet.
        assert gui.get_property("difficultyNextChange", "text") == "0.00%"
        assert gui.get_property("difficultyPreviousChange", "text") == "—"
        screenshot(gui, "difficulty-small.png")
        gui.invoke("widgetGrid", "resizeTo", ["difficulty-period", 3, 2])
        screenshot(gui, "difficulty-medium.png")

        hashes = rpc_call(port, "generatetodescriptor", [54, "raw(51)"])
        epoch_hash = hashes[-1]
        gui.wait_for_property("difficultyBlocksLeft", "text", "144 blocks left", timeout_ms=40000)
        assert gui.get_property("difficultyProgress", "text") == "0%"
        assert gui.get_property("difficultyPreviousChange", "text") == "0.00%"
        assert gui.get_property("difficultyAverageBlockTime", "text") == "—"

        start = rpc_call(port, "getblockheader", [epoch_hash])["time"]
        # Space two blocks exactly ten minutes apart to verify the displayed average.
        for offset in [600, 1200]:
            rpc_call(port, "setmocktime", [max(start, int(time.time())) + offset])
            rpc_call(port, "generatetodescriptor", [1, "raw(51)"])
        tip_hash = rpc_call(port, "getbestblockhash")
        tip = rpc_call(port, "getblockheader", [tip_hash])
        seconds = (tip["time"] - start) // 2
        expected_average = f"{seconds // 60}m {seconds % 60}s"
        gui.wait_for_property("difficultyAverageBlockTime", "text", expected_average, timeout_ms=40000)
        rpc_call(port, "invalidateblock", [epoch_hash])
        gui.wait_for_property("difficultyBlocksLeft", "text", "1 blocks left", timeout_ms=40000)
        assert gui.get_property("difficultyProgress", "text") == "99%"
        assert gui.get_property("difficultyPreviousChange", "text") == "—"
        rpc_call(port, "setmocktime", [0])
        first.stop(cleanup=False)

        reopened = QmlTestHarness(extra_args=args, datadir=first.datadir)
        gui = prepare(reopened)
        gui.wait_for_object("difficultyPeriodWidget")
        assert gui.get_property("widget_difficulty-period", "columnSpan") == 3
        assert gui.get_property("widget_difficulty-period", "rowSpan") == 2
        gui.set_property("appWindow", "width", 800)
        gui.set_property("appWindow", "height", 1100)
        gui.settle()
        screenshot(gui, "difficulty-portrait.png")
        choose_action(gui, "addWidgetButton")
        gui.wait_for_property("widgetPicker", "opened", True)
        assert gui.get_property("widgetPickerAdd_difficulty-period", "enabled") is False
        gui.invoke("widgetPicker", "close")
        gui.click("widgetRemove_difficulty-period")
        assert not gui.object_exists("difficultyPeriodWidget")
        print("Difficulty Period widget functional test PASSED")
    finally:
        if reopened:
            reopened.stop(cleanup=False)
        first.stop()


if __name__ == "__main__":
    run_tests()
