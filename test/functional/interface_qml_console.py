#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Keep secret command arguments out of a real GUI process's console and logs."""

from pathlib import Path
import re
from qml_scenarios import run_scenarios
from qml_smoke_lib import create_wallet, stop_gui_checked, wallet_scenario
from qml_wallet_test_lib import rpc_call, wait_for_rpc


# Public RpcConsoleModel::MessageCategory role values.
CMD_REQUEST, CMD_REPLY, CMD_ERROR = 0, 1, 2


def assert_complete_output_matches(gui, text, expected, *, expected_context="", reply_category=CMD_ERROR):
    """Search the complete current projection, including virtualized rows."""
    output = "consoleOutputArea_list"
    gui.wait_for_property(output, "model.busy", False, timeout_ms=30000)
    assert gui.get_property(output, "model.error") in (None, "")
    assert gui.get_property(output, "model.truncated") is False
    # SplitRows marks each original logical message with a timestamp. Only
    # a wallet command after Clear adds its exact context banner again.
    source = "model.sourceModel"
    original = gui.get_model_row(output, source)
    rows = original["rowCount"]
    assert 0 < rows <= 5000

    def read(row, role):
        return gui.get_model_row(output, source, row, role,
                                 model_id=original["modelId"], expected_rows=rows)["value"]

    starts = []
    context_messages = int(bool(expected_context))
    for row in range(rows):
        timestamp = read(row, "timestamp")
        if timestamp:
            assert re.fullmatch(r"[0-9]{2}:[0-9]{2}:[0-9]{2}", timestamp)
            assert read(row, "continuation") is False
            starts.append(row)
        assert 1 <= len(starts) <= 2 + context_messages
        # RpcConsoleModel's public CMD_REQUEST/REPLY/ERROR role values.
        assert read(row, "category") == (CMD_REQUEST if len(starts) <= 1 + context_messages else reply_category)
    assert len(starts) == 2 + context_messages, f"Unexpected logical messages {starts} in {rows} records"
    gui.set_property("rpcConsole", "searchText", text)
    gui.wait_for_property(output, "model.searchResultCount", expected, timeout_ms=30000)
    gui.wait_for_property(output, "model.searchBusy", False, timeout_ms=30000)
    assert gui.get_property(output, "model.searchText") == text
    assert gui.get_property(output, "model.searchLimited") is False
    assert gui.get_property(output, "model.busy") is False
    assert gui.get_property(output, "model.error") in (None, "")
    assert gui.get_property(output, "model.searchResultCount") == expected

    if expected_context:
        assert starts[:2] == [0, 1], "The exact context banner must occupy one record"
        prepared = gui.get_model_row(output, "model")

        def projected(role):
            return gui.get_model_row(output, "model", 0, role,
                                     model_id=prepared["modelId"], expected_rows=prepared["rowCount"])["value"]

        assert projected("plainContent") == expected_context
        assert projected("leftText") == read(0, "timestamp")
        assert projected("category") == CMD_REQUEST


def console_privacy(harness, checkpoint, wallet_enabled):
    harness.start_gui(extra_args=[] if wallet_enabled else ["-disablewallet"])
    gui = harness.driver
    wait_for_rpc(harness.gui_rpc_port)
    name = "privacy_process"
    if wallet_enabled:
        create_wallet(harness, name, "owned-fixture-password")
    before = rpc_call(harness.gui_rpc_port, "listwallets") if wallet_enabled else None
    if gui.get_property("appWindow", "desktopWalletMode"):
        gui.click("desktopWalletSettingsTabButton")
        gui.wait_for_property("desktopWalletSettingsTabButton", "checked", True)
    else:
        gui.click("nodeSettingsButton")
    gui.click("settingsSidebar_rpc-console")
    gui.wait_for_page("rpcConsole")
    secret = "unique-process-secret-92"
    commands = [
        f"createwallet {name} false false {secret}",
        f"migratewallet {name} {secret}",
        f"addhdkey {secret}",
        f"walletpassphrase {secret} 10",
    ]
    for command in commands:
        gui.set_text("consoleInput", command)
        gui.invoke("rpcConsole", "submitCommand", [command])
        gui.wait_for_property("rpcConsole", "executing", False, timeout_ms=30000)
        # Positive anchors certify the new request AND its completed reply
        # before a zero-match privacy assertion can be considered meaningful.
        context = f'Executing command using "{name}" wallet' if wallet_enabled else ""
        assert_complete_output_matches(gui, command.split()[0] + "(…)", 1, expected_context=context)
        assert_complete_output_matches(gui, "(code ", 1, expected_context=context)
        assert_complete_output_matches(gui, secret, 0, expected_context=context)
        gui.set_property("rpcConsole", "searchText", "")
        gui.invoke("rpcConsole", "clearInputOrOutput")
        gui.wait_for_property("rpcConsole", "outputCount", 0)
    checkpoint("secret-bearing requests and failures remain redacted")
    for command in [f"signrawtransactionwithkey deadbeef {secret}",
                    f"echo(signrawtransactionwithkey(deadbeef {secret}))"]:
        gui.set_text("consoleInput", command)
        gui.invoke("rpcConsole", "submitCommand", [command])
        gui.wait_for_property("rpcConsole", "executing", False, timeout_ms=30000)
        context = f'Executing command using "{name}" wallet' if wallet_enabled else ""
        assert_complete_output_matches(gui, "signrawtransactionwithkey(…)", 1, expected_context=context)
        assert_complete_output_matches(gui, "Invalid RPC arguments.", 1, expected_context=context)
        assert_complete_output_matches(gui, secret, 0, expected_context=context)
        gui.set_property("rpcConsole", "searchText", "")
        gui.invoke("rpcConsole", "clearInputOrOutput")
        gui.wait_for_property("rpcConsole", "outputCount", 0)
    checkpoint("malformed sensitive JSON never reaches Core RPC or exposes original input")
    gui.set_text("consoleInput", "help getblockcount")
    gui.invoke("rpcConsole", "submitCommand", ["help getblockcount"])
    gui.wait_for_property("rpcConsole", "executing", False, timeout_ms=30000)
    # One original request and one reply may span many bounded display rows.
    # Exact search matches establish both messages without assuming their
    # physical row count or instantiating every virtual delegate.
    context = f'Executing command using "{name}" wallet' if wallet_enabled else ""
    assert_complete_output_matches(gui, "help getblockcount", 1, expected_context=context, reply_category=CMD_REPLY)
    assert_complete_output_matches(gui, "Returns the height", 1, expected_context=context, reply_category=CMD_REPLY)
    if wallet_enabled:
        assert rpc_call(harness.gui_rpc_port, "listwallets") == before
        assert rpc_call(harness.gui_rpc_port, "getwalletinfo", wallet=name)["unlocked_until"] == 0
    stop_gui_checked(harness)
    logs = list(Path(harness.gui_datadir).rglob("debug.log"))
    assert logs, "No Core debug log was inspected"
    assert all(secret not in log.read_text(encoding="utf8", errors="replace") for log in logs)
    checkpoint("ordinary arguments work; shutdown stderr, exit and Core diagnostics inspected")


if __name__ == "__main__":
    raise SystemExit(run_scenarios("RPC argument privacy", [
        wallet_scenario("console-secrets-enabled", 3, lambda harness, checkpoint: console_privacy(harness, checkpoint, True)),
        wallet_scenario("console-secrets-disabled", 2, lambda harness, checkpoint: console_privacy(harness, checkpoint, False)),
    ]))
