#!/usr/bin/env python3
# Copyright (c) 2024 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""End-to-end tests for the RPC command console.

Starts the GUI as a regtest node (no peers needed), completes onboarding,
then navigates to Settings → Console and exercises command execution.

The console is only reachable after the node has fully started.  We use a
generous wait_for_page timeout (~90 s) for the initial node-runner screen
because the node must finish IBD initialisation before it can process
RPC commands.

History navigation (↑/↓ keys) is covered by the C++ unit tests in
test_rpcconsolemodel.cpp; the test bridge does not support key-event injection.

Requires:
  - bitcoin-core-app built with -DENABLE_TEST_AUTOMATION=ON
"""

import sys

from qml_test_harness import (
    QmlTestHarness,
    complete_onboarding,
    dump_qml_tree,
    parse_args,
)
from qml_driver import QmlDriver, QmlDriverError


# How long to wait for pages that require the node to be running.
# Node startup (IBD initialisation, wallet load, etc.) can take up to ~90 s
# on slow CI machines or when running with software rendering.
NODE_RUNNING_TIMEOUT_MS = 90_000


def _wait_for_node_settings_idle(gui, timeout_ms=5000):
    """Wait until the nested NodeSettings PageStack finishes animating."""
    gui.wait_for_property("nodeSettingsStack", "busy", False, timeout_ms=timeout_ms)


def navigate_to_console(gui):
    """From the NodeRunner main screen, navigate to the Console page.

    Waits for the node-runner screen (which only appears once the node is
    running), then clicks through Settings → Console.
    """
    # The nodeSettingsButton is on NodeRunner, which appears only once the
    # node has initialised — hence the generous timeout.
    gui.wait_for_page(
        "nodeSettingsButton",
        timeout_ms=NODE_RUNNING_TIMEOUT_MS,
    )
    gui.click("nodeSettingsButton")

    # Wait for the settingsConsole entry to appear in the NodeSettings page.
    gui.wait_for_page("settingsConsole", timeout_ms=5000)
    _wait_for_node_settings_idle(gui)

    gui.click("settingsConsole")
    gui.wait_for_page("commandConsole", timeout_ms=5000)
    _wait_for_node_settings_idle(gui)
    print("  Navigated to Console page.")


# ── Test cases ────────────────────────────────────────────────────────────────

def test_execute_getblockcount(gui):
    """Execute getblockcount and verify a request + reply pair appears (no error row)."""
    print("\n── test_execute_getblockcount ──────────────────────────────────")

    count_before = gui.get_property("commandConsole", "outputCount")
    gui.set_text("consoleInput", "getblockcount")
    gui.click("consoleSubmitButton")

    # Wait for execution to complete.
    gui.wait_for_property("commandConsole", "executing", False, timeout_ms=10000)

    count_after = gui.get_property("commandConsole", "outputCount")
    # Expect exactly 2 new rows: one CMD_REQUEST (command echo) and one
    # CMD_REPLY (the numeric block count).  An error would add a third row.
    assert count_after == count_before + 2, (
        f"Expected exactly 2 new output rows after getblockcount "
        f"(before={count_before}, after={count_after})"
    )
    print("  PASSED: getblockcount produced a request+reply pair without hanging the UI")


def test_execute_help(gui):
    """Execute 'help' and verify output rows appear."""
    print("\n── test_execute_help ───────────────────────────────────────────")

    count_before = gui.get_property("commandConsole", "outputCount")
    gui.set_text("consoleInput", "help")
    gui.click("consoleSubmitButton")

    gui.wait_for_property("commandConsole", "executing", False, timeout_ms=10000)

    count_after = gui.get_property("commandConsole", "outputCount")
    assert count_after > count_before, (
        f"Expected output rows after help (before={count_before}, after={count_after})"
    )
    print("  PASSED: help command produced output")


def test_execute_invalid_command(gui):
    """Execute an unknown command and verify the submit button re-enables and output appears."""
    print("\n── test_execute_invalid_command ────────────────────────────────")

    count_before = gui.get_property("commandConsole", "outputCount")
    gui.set_text("consoleInput", "thiscommanddoesnotexist")
    gui.click("consoleSubmitButton")

    # Wait for execution to complete (button stays disabled since input was cleared).
    gui.wait_for_property("commandConsole", "executing", False, timeout_ms=10000)

    count_after = gui.get_property("commandConsole", "outputCount")
    assert count_after > count_before, (
        f"Expected error output rows after invalid command (before={count_before}, after={count_after})"
    )
    print("  PASSED: invalid command handled; UI not blocked; error output appeared")


def test_back_navigation(gui):
    """Navigate back from the Console page and verify we return to Settings."""
    print("\n── test_back_navigation ────────────────────────────────────────")

    gui.click("consoleBackButton")
    # After pressing Back we should land on the NodeSettings stack.
    gui.wait_for_page("nodeSettingsStack", timeout_ms=5000)
    print("  PASSED: back navigation returned to Settings")


# ── Entry point ───────────────────────────────────────────────────────────────

def main():
    args = parse_args()
    harness = QmlTestHarness(socket_path=args.socket_path, extra_args=["-disablewallet"])

    try:
        harness.start()
        gui = harness.driver

        # Complete the onboarding wizard to reach the main screen.
        complete_onboarding(gui)

        # Navigate to the Console page (requires a running node).
        navigate_to_console(gui)

        # Run the test cases.
        test_execute_getblockcount(gui)
        test_execute_help(gui)
        test_execute_invalid_command(gui)
        test_back_navigation(gui)

        print("\nAll console tests passed.")

    except Exception as exc:
        print(f"\nTest FAILED: {exc}")
        dump_qml_tree(harness.driver)
        sys.exit(1)

    finally:
        harness.stop()


if __name__ == "__main__":
    main()
