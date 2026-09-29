#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Exercise the Mempool summary and network-specific halving in the real app."""

from qml_test_harness import QmlTestHarness
from qml_test_widget_dashboard import prepare, choose_action
from qml_wallet_test_lib import RPC_USER, RPC_PASS, rpc_call, wait_for_rpc


def add(gui, widget_id):
    choose_action(gui, "addWidgetButton")
    gui.wait_for_property("widgetPicker", "opened", True)
    gui.click("widgetPickerAdd_" + widget_id)
    gui.wait_for_object("widget_" + widget_id)
    choose_action(gui, "editWidgetsButton")


def run_tests():
    args = ["-disablewallet", f"-rpcuser={RPC_USER}", f"-rpcpassword={RPC_PASS}"]
    first = QmlTestHarness(extra_args=args)
    reopened = None
    try:
        gui = prepare(first)
        port = first.rpc_port
        wait_for_rpc(port)
        add(gui, "mempool-summary")
        gui.wait_for_property("mempoolSummaryWidget", "ready", True, timeout_ms=20000)
        assert gui.get_property("mempoolSummaryWidget", "queuedBlocks") == 0
        assert gui.get_property("mempoolSummaryCount", "text") == "0"
        add(gui, "mempool")
        gui.wait_for_property("mempoolWidget", "ready", True, timeout_ms=20000)
        choose_action(gui, "editWidgetsButton")
        gui.click("widgetRemove_mempool")
        choose_action(gui, "editWidgetsButton")
        assert gui.get_property("mempoolSummaryWidget", "ready") is True
        gui.invoke("widgetGrid", "resizeTo", ["mempool-summary", 1, 1])

        add(gui, "halving")
        hashes = rpc_call(port, "generatetodescriptor", [149, "raw(51)"])
        gui.wait_for_property("halvingBlocksRemaining", "text", "1", timeout_ms=30000)
        assert gui.get_property("halvingWidget", "percent") == 99
        rpc_call(port, "generatetodescriptor", [1, "raw(51)"])
        gui.wait_for_property("halvingBlocksRemaining", "text", "150", timeout_ms=30000)
        assert gui.get_property("halvingWidget", "percent") == 0
        assert gui.get_property("halvingSubsidy", "text") == "25 → 12.5"
        rpc_call(port, "invalidateblock", [hashes[-1]])
        gui.wait_for_property("halvingBlocksRemaining", "text", "2", timeout_ms=30000)
        first.stop(cleanup=False)
        reopened = QmlTestHarness(extra_args=args, datadir=first.datadir)
        gui = prepare(reopened)
        gui.wait_for_object("widget_mempool-summary")
        assert gui.get_property("widget_mempool-summary", "columnSpan") == 1
        assert gui.get_property("widget_mempool-summary", "rowSpan") == 1
        gui.wait_for_property("halvingBlocksRemaining", "text", "2", timeout_ms=30000)
        print("Mempool and Halving integration tests PASSED")
    finally:
        if reopened:
            reopened.stop(cleanup=False)
        first.stop()


if __name__ == "__main__":
    run_tests()
