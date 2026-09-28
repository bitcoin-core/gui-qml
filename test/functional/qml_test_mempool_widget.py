#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Verify accepted arrivals, mining and layout persistence without a wallet."""

from pathlib import Path
import sys

from qml_test_harness import QmlTestHarness
from qml_test_widget_dashboard import prepare, screenshot, choose_action
from qml_wallet_test_lib import RPC_USER, RPC_PASS, rpc_call, wait_for_rpc

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "bitcoin" / "test" / "functional"))
from test_framework.messages import CTransaction, CTxIn, CTxOut, COutPoint, COIN


def geometry(gui):
    return tuple(gui.get_property("widget_mempool", name)
                 for name in ("gridColumn", "gridRow", "columnSpan", "rowSpan"))


def run_tests():
    args = ["-disablewallet", "-acceptnonstdtxn=1", f"-rpcuser={RPC_USER}", f"-rpcpassword={RPC_PASS}"]
    first = QmlTestHarness(extra_args=args)
    reopened = None
    try:
        gui = prepare(first)
        port = first.rpc_port
        wait_for_rpc(port)
        choose_action(gui, "addWidgetButton")
        gui.wait_for_property("widgetPicker", "opened", True)
        gui.click("widgetPickerAdd_mempool")
        gui.wait_for_object("mempoolWidget")
        assert geometry(gui)[2:] == (2, 1)
        assert gui.get_property("widget_mempool", "supportedSizes") == [
            {"columns": 2, "rows": 1, "label": "Medium"}, {"columns": 3, "rows": 2, "label": "Large"},
            {"columns": 1, "rows": 1, "label": "Small"}]
        choose_action(gui, "editWidgetsButton")
        gui.wait_for_property("mempoolWidget", "ready", True)
        gui.wait_for_property("mempoolWidget", "currentRate", 0, timeout_ms=15000)
        # Mature an anyone-can-spend coinbase entirely inside this regtest node.
        blocks = rpc_call(port, "generatetodescriptor", [101, "raw(51)"])
        coinbase = rpc_call(port, "getblock", [blocks[0], 2])["tx"][0]
        tx = CTransaction()
        tx.vin = [CTxIn(COutPoint(int(coinbase["txid"], 16), 0))]
        # Include a small data output to exceed Core's minimum transaction size.
        tx.vout = [CTxOut(50 * COIN - 1000, b"\x51"), CTxOut(0, b"\x6a\x04test")]
        rpc_call(port, "sendrawtransaction", [tx.serialize().hex()])
        gui.wait_for_property("mempoolWidget", "currentRate", lambda value: value > 0, timeout_ms=15000)
        assert rpc_call(port, "getmempoolinfo")["size"] == 1
        assert any(point["rate"] > 0 for point in gui.get_property("mempoolWidget", "samples"))
        screenshot(gui, "mempool-compact.png")
        gui.invoke("widgetGrid", "resizeTo", ["mempool", 3, 2])
        assert geometry(gui)[2:] == (3, 2)
        expected = geometry(gui)
        screenshot(gui, "mempool-expanded.png")
        first.stop(cleanup=False)

        reopened = QmlTestHarness(extra_args=args, datadir=first.datadir)
        gui = prepare(reopened)
        gui.wait_for_object("mempoolWidget")
        assert geometry(gui) == expected
        gui.wait_for_property("mempoolWidget", "ready", True)
        assert rpc_call(port, "getmempoolinfo")["size"] == 1
        gui.wait_for_property("mempoolWidget", "currentRate", 0, timeout_ms=15000)
        # Loading the saved mempool must not count as new arrivals.
        assert all(point["rate"] <= 0 for point in gui.get_property("mempoolWidget", "samples"))
        rpc_call(port, "generatetodescriptor", [1, "raw(51)"])
        assert rpc_call(port, "getmempoolinfo")["size"] == 0
        gui.wait_for_property("mempoolWidget", "currentRate", 0, timeout_ms=15000)
        assert all(point["rate"] <= 0 for point in gui.get_property("mempoolWidget", "samples"))
        gui.set_property("appWindow", "width", 800)
        gui.set_property("appWindow", "height", 1100)
        gui.settle()
        screenshot(gui, "mempool-portrait.png")
        assert geometry(gui)[2:] == (3, 2)
        choose_action(gui, "addWidgetButton")
        gui.wait_for_property("widgetPicker", "opened", True)
        assert gui.get_property("widgetPickerAdd_mempool", "enabled") is False
        gui.invoke("widgetPicker", "close")
        gui.click("widgetRemove_mempool")
        assert not gui.object_exists("mempoolWidget")
        print("Mempool widget functional test PASSED")
    finally:
        if reopened:
            reopened.stop(cleanup=False)
        first.stop()


if __name__ == "__main__":
    run_tests()
