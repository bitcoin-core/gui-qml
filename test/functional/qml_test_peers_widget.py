#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Verify live peer directions, network grouping, sizes and restart persistence."""

import time

from qml_test_harness import QmlTestHarness, pick_unused_port
from qml_test_widget_dashboard import prepare, choose_action, screenshot
from qml_wallet_test_lib import RPC_USER, RPC_PASS, rpc_call, wait_for_rpc


def wait_for_peers(port, count):
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        peers = rpc_call(port, "getpeerinfo")
        if len(peers) == count and all(peer["version"] > 0 for peer in peers):
            return peers
        time.sleep(0.1)
    raise AssertionError(f"Expected {count} peers, got {peers}")


def run_tests():
    args = ["-disablewallet", f"-rpcuser={RPC_USER}", f"-rpcpassword={RPC_PASS}"]
    first_port, second_port = pick_unused_port(), pick_unused_port()
    first_args = args + ["-listen=1", f"-bind=127.0.0.1:{first_port}"]
    first = QmlTestHarness(extra_args=first_args, no_listen_arg=False)
    second = QmlTestHarness(extra_args=args + ["-listen=1", f"-bind=127.0.0.1:{second_port}"], no_listen_arg=False)
    reopened = None
    try:
        gui = prepare(first)
        second.start()
        wait_for_rpc(first.rpc_port)
        wait_for_rpc(second.rpc_port)
        choose_action(gui, "addWidgetButton")
        gui.wait_for_property("widgetPicker", "opened", True)
        gui.click("widgetPickerRow_peers")
        gui.click("widgetPickerSize_peers_2x1")
        gui.wait_for_object("peersWidget")
        choose_action(gui, "editWidgetsButton")
        gui.wait_for_property("peersWidget", "ready", True)
        gui.wait_for_property("peersWidgetTotal", "text", "0")

        rpc_call(first.rpc_port, "addnode", [f"127.0.0.1:{second_port}", "onetry"])
        peers = wait_for_peers(first.rpc_port, 1)
        assert not peers[0]["inbound"]
        gui.wait_for_property("peersWidgetOutbound", "text", "1 outbound")
        assert gui.get_property("peersWidgetInbound", "text") == "0 inbound"
        gui.invoke("widgetGrid", "resizeTo", ["peers", 1, 1])
        gui.wait_for_property("peersWidgetCompactOutbound", "text", "1 outbound")
        screenshot(gui, "peers-small.png")
        gui.invoke("widgetGrid", "resizeTo", ["peers", 3, 2])
        # Core classifies loopback as not publicly routable, so it belongs to
        # Other. Manual connections retain their network bucket.
        network = {"ipv4": "ipv4", "ipv6": "ipv6", "onion": "tor", "i2p": "i2p", "cjdns": "cjdns"}.get(peers[0]["network"], "other")
        gui.wait_for_object("peersWidgetGroup_" + network)
        assert gui.get_property("peersWidgetGroupCount_" + network, "text") == "1"
        assert gui.get_property("peersWidgetChart", "total") == 1
        assert not gui.object_exists("peersWidgetGroup_manual")
        screenshot(gui, "peers-large.png")

        # Reverse the connection to exercise inbound updates and a true zero.
        rpc_call(first.rpc_port, "disconnectnode", [peers[0]["addr"]])
        wait_for_peers(first.rpc_port, 0)
        gui.wait_for_property("peersWidgetStatus", "text", "No connections")
        rpc_call(second.rpc_port, "addnode", [f"127.0.0.1:{first_port}", "onetry"])
        peers = wait_for_peers(first.rpc_port, 1)
        assert peers[0]["inbound"]
        gui.wait_for_property("peersWidgetInbound", "text", "1")
        assert gui.get_property("peersWidgetOutbound", "text") == "0"
        assert gui.get_property("peersWidget", "inboundFraction") == 1

        first.stop(cleanup=False)
        reopened = QmlTestHarness(extra_args=first_args, datadir=first.datadir, no_listen_arg=False)
        gui = prepare(reopened)
        gui.wait_for_object("peersWidget")
        assert gui.get_property("widget_peers", "columnSpan") == 3
        assert gui.get_property("widget_peers", "rowSpan") == 2
        gui.wait_for_property("peersWidgetTotal", "text", "0")
        choose_action(gui, "editWidgetsButton")
        gui.click("widgetRemove_peers")
        assert not gui.object_exists("peersWidget")
        print("Peers widget live connections and persistence test PASSED")
    finally:
        if reopened:
            reopened.stop(cleanup=False)
        second.stop()
        first.stop()


if __name__ == "__main__":
    run_tests()
