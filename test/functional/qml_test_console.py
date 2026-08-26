#!/usr/bin/env python3
# Copyright (c) 2024 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""End-to-end tests for the RPC command console.

Starts the GUI as a regtest node (no peers needed), completes onboarding,
then navigates to Settings → RPC console and exercises command execution.

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
import re

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


def navigate_to_console(gui):
    """From the NodeRunner main screen, navigate to the RPC console settings page.

    Waits for the node-runner screen (which only appears once the node is
    running), then opens Settings and selects RPC console in the sidebar.
    """
    gui.wait_for_page(
        "nodeSettingsButton",
        timeout_ms=NODE_RUNNING_TIMEOUT_MS,
    )
    gui.click("nodeSettingsButton")
    gui.wait_for_property("settingsSidebar_rpc-console", "visible", True, timeout_ms=5000)
    gui.click("settingsSidebar_rpc-console")
    gui.wait_for_page("rpcConsoleSettingsPage", timeout_ms=5000)
    gui.wait_for_page("rpcConsole", timeout_ms=5000)
    # The command input auto-focuses on open (desktop), mirroring Core's
    # RPCConsole, so the user can type immediately.
    gui.wait_for_property("consoleInput", "activeFocus", True, timeout_ms=5000)
    print("  Navigated to Console page; command input auto-focused.")


# ── Test cases ────────────────────────────────────────────────────────────────

def assert_close(actual, expected, label, tolerance=1):
    assert abs(actual - expected) <= tolerance, (
        f"{label}: expected {expected}, got {actual}"
    )


def submit_console_command(gui, command):
    gui.set_text("consoleInput", command)
    gui.invoke("rpcConsole", "runHighlightedOrSubmit")


def test_console_page_matches_design(gui):
    """Console page uses the settings layout, toolbar, and command footer."""
    print("\n── test_console_page_matches_design ────────────────────────────")

    root_width = gui.get_property("rpcConsole", "width")
    row_x = gui.get_property("consoleInputRow", "x")
    row_width = gui.get_property("consoleInputRow", "width")
    row_height = gui.get_property("consoleInputRow", "height")
    divider_width = gui.get_property("consoleInputDivider", "width")
    divider_height = gui.get_property("consoleInputDivider", "height")
    content_x = gui.get_property("consoleInputContent", "x")
    content_y = gui.get_property("consoleInputContent", "y")
    content_height = gui.get_property("consoleInputContent", "height")
    prompt_width = gui.get_property("consolePromptIcon", "width")
    prompt_height = gui.get_property("consolePromptIcon", "height")
    input_x = gui.get_property("consoleInput", "x")

    assert_close(row_x, 0, "console input row x")
    assert_close(row_width, root_width, "console input row width")
    assert_close(row_height, 64, "console input row height")
    assert_close(divider_width, row_width, "console input divider width")
    assert_close(divider_height, 1, "console footer separator height")
    assert_close(content_x, 12, "console input content x")
    assert_close(content_y, 12, "console input content y")
    assert_close(content_height, 40, "console input content height")
    assert_close(prompt_width, 16, "console prompt icon width")
    assert_close(prompt_height, 16, "console prompt icon height")
    assert_close(content_x + input_x, 36, "console text field x within row")
    assert gui.get_property("consoleInput", "placeholderText") == "Enter command…"
    assert gui.get_property("rpcConsoleSearchField", "placeholderText") == "Search console"
    assert gui.get_property("rpcConsoleSearchPreviousButton", "enabled") is False
    assert gui.get_property("rpcConsoleSearchNextButton", "enabled") is False
    assert gui.get_property("rpcConsoleWarningBanner", "text") == (
        "Beware of scammers who may ask you to enter commands here to steal your funds. "
        "Only enter commands you fully understand."
    )

    gui.click("consoleFontIncreaseButton")
    assert gui.get_property("rpcConsole", "outputFontPixelSize") == 14
    gui.click("consoleFontDecreaseButton")
    assert gui.get_property("rpcConsole", "outputFontPixelSize") == 13

    print("  PASSED: settings toolbar, font stepper, and command footer match the design")


def assert_console_entry_geometry(gui, index, row_width):
    row_name = f"consoleOutputArea_row_{index}"
    left_name = f"consoleOutputArea_left_{index}"
    content_name = f"consoleOutputArea_content_{index}"

    gui.wait_for_property(row_name, "visible", True, timeout_ms=3000)
    assert_close(gui.get_property(row_name, "width"), row_width, f"console entry {index} row width")
    assert_close(gui.get_property(left_name, "x"), 0, f"console entry {index} time x")
    assert_close(gui.get_property(left_name, "width"), 60, f"console entry {index} time width")
    assert_close(gui.get_property(content_name, "x"), 80, f"console entry {index} content x")


def test_console_output_rows_match_design(gui):
    """Console output rows follow the Figma Console entry component geometry."""
    print("\n── test_console_output_rows_match_design ───────────────────────")

    assert gui.get_property("rpcConsole", "outputCount") == 0
    root_width = gui.get_property("rpcConsole", "width")
    column_width = root_width - 32

    assert_close(gui.get_property("consoleOutputArea_contentColumn", "x"), 16, "console output column x")
    assert_close(gui.get_property("consoleOutputArea_contentColumn", "width"), column_width, "console output column width")
    assert_close(gui.get_property("consoleOutputArea_contentColumn", "topPadding"), 16, "console output top padding")
    help_text = gui.get_text("rpcConsoleHelpFooter")
    assert help_text == (
        "Use ↑↓ arrows to navigate history. Type help for an overview of available commands. "
        "Type help-console for console syntax help."
    )

    count_before = gui.get_property("rpcConsole", "outputCount")
    submit_console_command(gui, "getblockcount")
    gui.wait_for_property("rpcConsole", "executing", False, timeout_ms=10000)
    gui.wait_for_property("rpcConsole", "outputCount", count_before + 2, timeout_ms=3000)

    request_index = count_before
    reply_index = count_before + 1
    assert_console_entry_geometry(gui, request_index, column_width)
    assert_console_entry_geometry(gui, reply_index, column_width)
    assert gui.get_property(f"consoleOutputArea_row_{request_index}", "rowCategory") == 0
    assert gui.get_property(f"consoleOutputArea_row_{reply_index}", "rowCategory") == 1

    request_time = gui.get_text(f"consoleOutputArea_left_{request_index}")
    assert re.fullmatch(r"\d\d:\d\d:\d\d", request_time), f"Unexpected request timestamp: {request_time!r}"
    request_text = gui.get_text(f"consoleOutputArea_content_{request_index}")
    assert "getblockcount" in request_text
    assert "&gt;&gt;" not in request_text

    # Searching selects the matching occurrence without removing any rows.
    output_count = gui.get_property("rpcConsole", "outputCount")
    gui.set_text("rpcConsoleSearchField", "getblockcount")
    gui.wait_for_property("rpcConsole", "searchResultCount", 1, timeout_ms=3000)
    assert gui.get_property("rpcConsole", "outputCount") == output_count
    assert gui.get_property("rpcConsoleSearchPreviousButton", "enabled") is True
    assert gui.get_property("rpcConsoleSearchNextButton", "enabled") is True
    assert gui.get_property(
        f"consoleOutputArea_content_{request_index}", "selectedText"
    ).lower() == "getblockcount"
    gui.click("rpcConsoleSearchNextButton")
    assert gui.get_property("rpcConsole", "currentSearchResultIndex") == 0
    gui.set_text("rpcConsoleSearchField", "")
    gui.wait_for_property("rpcConsole", "searchResultCount", 0, timeout_ms=3000)
    print("  PASSED: console output entry geometry, timestamps, and categories match design")


def test_execute_getblockcount(gui):
    """Execute getblockcount and verify a request + reply pair appears (no error row)."""
    print("\n── test_execute_getblockcount ──────────────────────────────────")

    count_before = gui.get_property("rpcConsole", "outputCount")
    submit_console_command(gui, "getblockcount")

    # Wait for execution to complete.
    gui.wait_for_property("rpcConsole", "executing", False, timeout_ms=10000)

    count_after = gui.get_property("rpcConsole", "outputCount")
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

    count_before = gui.get_property("rpcConsole", "outputCount")
    submit_console_command(gui, "help")

    gui.wait_for_property("rpcConsole", "executing", False, timeout_ms=10000)

    count_after = gui.get_property("rpcConsole", "outputCount")
    assert count_after > count_before, (
        f"Expected output rows after help (before={count_before}, after={count_after})"
    )
    print("  PASSED: help command produced output")


def test_execute_invalid_command(gui):
    """Execute an unknown command and verify the submit button re-enables and output appears."""
    print("\n── test_execute_invalid_command ────────────────────────────────")

    count_before = gui.get_property("rpcConsole", "outputCount")
    submit_console_command(gui, "thiscommanddoesnotexist")

    # Wait for execution to complete (button stays disabled since input was cleared).
    gui.wait_for_property("rpcConsole", "executing", False, timeout_ms=10000)

    count_after = gui.get_property("rpcConsole", "outputCount")
    assert count_after > count_before, (
        f"Expected error output rows after invalid command (before={count_before}, after={count_after})"
    )
    print("  PASSED: invalid command handled; UI not blocked; error output appeared")


def test_autocomplete_popup_appears(gui):
    """Typing a partial command should open the autocomplete popup."""
    print("\n── test_autocomplete_popup_appears ─────────────────────────────")

    gui.set_text("consoleInput", "getblock")
    # Popup should become visible since "getblock" matches commands like getblockcount
    gui.wait_for_property("consoleAutocompletePopup", "visible", True, timeout_ms=3000)
    popup_x = gui.get_property("consoleAutocompletePopup", "x")
    field_x = (
        gui.get_property("consoleInputContent", "x")
        + gui.get_property("consoleInput", "x")
    )
    assert_close(popup_x, field_x, "autocomplete menu left alignment")
    assert_close(gui.get_property("consoleAutocomplete_0", "height"), 36,
                 "autocomplete context-menu item height")
    print("  PASSED: autocomplete popup appeared for partial command")
    # Clear for next test
    gui.set_text("consoleInput", "")


def test_autocomplete_popup_hidden_no_match(gui):
    """Typing text with no command matches should not show the popup."""
    print("\n── test_autocomplete_popup_hidden_no_match ─────────────────────")

    gui.set_text("consoleInput", "zzzznotacommand")
    visible = gui.get_property("consoleAutocompletePopup", "visible")
    assert visible == False, f"Expected popup hidden for no-match input, got {visible}"
    print("  PASSED: autocomplete popup hidden for non-matching input")
    gui.set_text("consoleInput", "")


def test_autocomplete_click_applies_suggestion(gui):
    """Clicking an autocomplete suggestion should fill the input field."""
    print("\n── test_autocomplete_click_applies_suggestion ───────────────────")

    gui.set_text("consoleInput", "getblockcou")
    # Wait for popup to appear
    gui.wait_for_property("consoleAutocompletePopup", "visible", True, timeout_ms=3000)
    # Click first suggestion
    gui.click("consoleAutocomplete_0")
    # Input should now contain the completed command + trailing space
    text = gui.get_text("consoleInput")
    assert text.strip() == "getblockcount", f"Expected 'getblockcount', got '{text.strip()}'"
    print("  PASSED: clicking autocomplete suggestion filled input field")
    gui.set_text("consoleInput", "")


def test_autocomplete_help_variants(gui):
    """Typing 'help get' should show help variants in autocomplete."""
    print("\n── test_autocomplete_help_variants ─────────────────────────────")

    gui.set_text("consoleInput", "help get")
    gui.wait_for_property("consoleAutocompletePopup", "visible", True, timeout_ms=3000)
    print("  PASSED: autocomplete popup appeared for 'help get' prefix")
    gui.set_text("consoleInput", "")


def test_back_navigation(gui):
    """Close Settings from the RPC console and verify we return to NodeRunner."""
    print("\n── test_back_navigation ────────────────────────────────────────")

    gui.click("settingsDoneButton")
    gui.wait_for_page("nodeRunner", timeout_ms=5000)
    print("  PASSED: back navigation returned to NodeRunner")


def test_clear_removes_output_and_keeps_help_footer(gui):
    """Clearing removes console rows while keeping help outside the card."""
    print("\n── test_clear_removes_output_and_keeps_help_footer ─────────────")

    gui.set_text("consoleInput", "")
    assert gui.get_property("rpcConsole", "outputCount") > 0

    gui.invoke("rpcConsole", "clearInputOrOutput")
    gui.wait_for_property("rpcConsole", "outputCount", 0, timeout_ms=3000)
    assert gui.get_text("rpcConsoleHelpFooter") == (
        "Use ↑↓ arrows to navigate history. Type help for an overview of available commands. "
        "Type help-console for console syntax help."
    )
    print("  PASSED: clear removed output and retained the external help footer")


# ── Entry point ───────────────────────────────────────────────────────────────

def main():
    args = parse_args()
    harness = QmlTestHarness(socket_path=args.socket_path, extra_args=["-disablewallet", "-qwindowgeometry", "800x700"])

    try:
        harness.start()
        gui = harness.driver

        # Complete the onboarding wizard to reach the main screen.
        complete_onboarding(gui)

        # Navigate to the Console page (requires a running node).
        navigate_to_console(gui)

        # Run the test cases.
        test_console_page_matches_design(gui)
        test_console_output_rows_match_design(gui)
        test_execute_getblockcount(gui)
        test_execute_help(gui)
        test_execute_invalid_command(gui)
        test_autocomplete_popup_appears(gui)
        test_autocomplete_popup_hidden_no_match(gui)
        test_autocomplete_click_applies_suggestion(gui)
        test_autocomplete_help_variants(gui)
        test_clear_removes_output_and_keeps_help_footer(gui)
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
