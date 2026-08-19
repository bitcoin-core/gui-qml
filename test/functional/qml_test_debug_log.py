#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the in-app Debug Log viewer.

Walks through Settings → Debug Log and verifies:
  1. The viewer page loads with a visible search bar and log list.
  2. The initial load is capped while older entries remain available.
  3. Typing in the search field filters the list; clearing it restores
     the full count.
  4. Auto-refresh replaces the capped tail without growing it unboundedly.
  5. The list virtualizes offscreen rows and can load older rows on demand.

This test requires the binary to be built with -DENABLE_TEST_AUTOMATION=ON.
"""

import datetime
import os
import shutil
import signal
import subprocess
import sys
import tempfile

from qml_test_harness import GUI_STARTUP_TIMEOUT, dump_qml_tree, find_gui_binary, setup_datadir
from qml_driver import QmlDriver


INITIAL_LOAD_LIMIT = 1000
SEEDED_HISTORY_LINES = 1200


def assert_close(actual, expected, label, tolerance=1):
    assert abs(actual - expected) <= tolerance, (
        f"{label}: expected {expected}, got {actual}"
    )


# ── Harness ───────────────────────────────────────────────────────────────────

class DebugLogHarness:
    """Launches the GUI node as an onboarded profile on NodeRunner."""

    def __init__(self):
        self.gui_binary = find_gui_binary()
        self.tmpdir = tempfile.mkdtemp(prefix="qml_test_debug_log_")
        self.socket_path = os.path.join(self.tmpdir, "test_bridge.sock")
        self.process = None
        self.driver = None
        self.datadir = setup_datadir(self.tmpdir)
        self._seed_debug_log_history()

    def _seed_debug_log_history(self):
        """Create enough history to exercise the initial cap and Load more."""
        network_dir = os.path.join(self.datadir, "regtest")
        os.makedirs(network_dir, exist_ok=True)
        log_path = os.path.join(network_dir, "debug.log")
        with open(log_path, "w", encoding="utf-8") as f:
            for i in range(SEEDED_HISTORY_LINES):
                f.write(
                    "2026-01-01T00:00:00Z "
                    f"test-automation seeded-history marker {i}\n"
                )

    def start(self):
        env = dict(os.environ)
        env["QT_QPA_PLATFORM"] = "offscreen"
        args = [
            self.gui_binary,
            f"-datadir={self.datadir}",
            f"-test-automation={self.socket_path}",
            # Runtime tests are not exercising first-run onboarding.
            # -disablewallet forces AppMode.walletEnabled=false so MainWindow
            # routes to the node/NodeRunner stack instead of desktopWallets.
            "-qml_onboarded=1",
            "-disablewallet",
            "-logtimemicros",
            "-debug",
            "-debugexclude=leveldb",
            "-nolisten",
        ]
        print(f"Starting GUI: {' '.join(args)}")
        self.process = subprocess.Popen(
            args, env=env,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
        self.driver = QmlDriver(self.socket_path, timeout=GUI_STARTUP_TIMEOUT)
        print("QmlDriver connected to test bridge.")

    def stop(self):
        if self.process and self.process.poll() is None:
            self.process.send_signal(signal.SIGTERM)
            try:
                self.process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
        if self.driver:
            self.driver.close()
        if self.tmpdir:
            shutil.rmtree(self.tmpdir, ignore_errors=True)
            self.tmpdir = None


# ── Navigation ────────────────────────────────────────────────────────────────

def navigate_to_debug_log(gui):
    """From the NodeRunner main screen, navigate to the Debug Log page.

    Requires -disablewallet so AppMode.walletEnabled is false and MainWindow
    routes to the node/NodeRunner stack rather than desktopWallets.
    """
    gui.wait_for_page("nodeRunner", timeout_ms=10000)
    gui.click("nodeSettingsButton")
    gui.wait_for_page("settingsView", timeout_ms=5000)
    gui.wait_for_property("settingsSidebar_debug-log", "visible", True, timeout_ms=5000)
    gui.click("settingsSidebar_debug-log")
    gui.wait_for_page("settingsDebugLog", timeout_ms=5000)


# ── Test cases ────────────────────────────────────────────────────────────────

def test_viewer_visibility(gui):
    """Verify the search bar and log list are visible on the debug log page."""
    print("\n── test_viewer_visibility ────────────────────────────────────────")
    assert gui.get_property("debugLogSearchField", "visible"), \
        "debugLogSearchField is not visible"
    assert gui.get_property("debugLogListView", "visible"), \
        "debugLogListView is not visible"
    print("  PASSED: search bar and log list are visible")


def test_log_has_entries(gui):
    """Verify that the first snapshot is capped at 1,000 rows."""
    print("\n── test_log_has_entries ──────────────────────────────────────────")
    count = gui.wait_for_property(
        "debugLogListView", "count", INITIAL_LOAD_LIMIT, timeout_ms=5000
    )
    assert count == INITIAL_LOAD_LIMIT, (
        f"Expected the initial load to be capped at {INITIAL_LOAD_LIMIT}, "
        f"got count={count}"
    )
    print(f"  PASSED: debugLogListView.count = {count}")
    return count


def test_search_layout_matches_design(gui):
    """Verify the search row, divider, and first log row follow the design geometry."""
    print("\n── test_search_layout_matches_design ────────────────────────────")

    gui.wait_for_property("debugLogSearchField", "height", 44, timeout_ms=3000)
    gui.wait_for_property("debugLogSearchDivider", "height", 1, timeout_ms=3000)
    gui.wait_for_property("debugLogListView", "topPadding", 10, timeout_ms=3000)
    gui.wait_for_property("debugLogListView_row_0", "visible", True, timeout_ms=3000)
    gui.wait_for_property("debugLogListView_lineNumber_0", "visible", True, timeout_ms=3000)
    gui.wait_for_property("debugLogListView_date_0", "visible", True, timeout_ms=3000)

    first_row_has_command = gui.get_property("debugLogListView_command_0", "visible")
    if first_row_has_command:
        gui.wait_for_property("debugLogListView_message_0", "visible", True, timeout_ms=3000)
    else:
        gui.wait_for_property("debugLogListView_commandlessMessage_0", "visible", True, timeout_ms=3000)
        gui.wait_for_property("debugLogListView_message_0", "visible", False, timeout_ms=3000)

    search_row_y = gui.get_property("debugLogSearchRow", "y")
    search_row_height = gui.get_property("debugLogSearchRow", "height")
    search_height = gui.get_property("debugLogSearchField", "height")
    divider_y = gui.get_property("debugLogSearchDivider", "y")
    divider_height = gui.get_property("debugLogSearchDivider", "height")
    list_y = gui.get_property("debugLogListView", "y")
    first_row_viewport_y = (
        gui.get_property("debugLogListView_row_0", "y")
        - gui.get_property("debugLogListView", "contentY")
    )
    effective_line_number_width = gui.get_property(
        "debugLogListView", "effectiveLineNumberWidth"
    )
    page_width = gui.get_property("settingsDebugLog", "width")
    content_width = gui.get_property("debugLogContentLayout", "width")
    content_horizontal_padding = gui.get_property(
        "settingsDebugLog", "contentHorizontalPadding"
    )
    maximum_content_width = gui.get_property(
        "settingsDebugLog", "maximumContentWidth"
    )
    expected_content_width = max(
        0,
        min(page_width - content_horizontal_padding * 2, maximum_content_width),
    )

    assert_close(content_width, expected_content_width,
                 "debug log content max width")
    assert_close(gui.get_property("debugLogContentLayout", "x"),
                 (page_width - content_width) / 2,
                 "debug log content horizontal centering")
    assert_close(search_row_height, 44, "debug log search row height")
    assert_close(gui.get_property("debugLogSearchRow", "spacing"), 10,
                 "debug log search row spacing")
    assert_close(search_height, 44, "debug log search row height")
    assert_close(gui.get_property("debugLogSearchField", "leftPadding"), 0,
                 "debug log search left padding")
    assert_close(gui.get_property("debugLogSearchField", "font.pixelSize"), 15,
                 "debug log search font size")
    assert_close(gui.get_property("debugLogSearchIcon", "width"), 24,
                 "debug log search icon width")
    assert_close(gui.get_property("debugLogSearchIcon", "height"), 24,
                 "debug log search icon height")
    assert_close(gui.get_property("debugLogRefreshButton", "width"), 20,
                 "debug log refresh button width")
    assert_close(gui.get_property("debugLogRefreshButton", "height"), 20,
                 "debug log refresh button height")
    assert_close(gui.get_property("debugLogRefreshIcon", "width"), 20,
                 "debug log refresh icon width")
    assert_close(gui.get_property("debugLogRefreshIcon", "height"), 20,
                 "debug log refresh icon height")
    assert_close(
        gui.get_property("debugLogRefreshButton", "x") +
        gui.get_property("debugLogRefreshButton", "width"),
        gui.get_property("debugLogSearchRow", "width"),
        "debug log refresh button right alignment",
    )
    assert_close(divider_height, 1, "debug log search divider height")
    assert_close(divider_y, search_row_y + search_row_height,
                 "debug log search divider y")
    assert_close(list_y, divider_y + divider_height,
                 "debug log list y")
    assert_close(gui.get_property("debugLogListView", "topPadding"), 10,
                 "debug log list top padding")
    assert_close(gui.get_property("debugLogListView", "rowSpacing"), 10,
                 "debug log entry row spacing")
    assert_close(gui.get_property("debugLogListView", "columnSpacing"), 10,
                 "debug log entry column spacing")
    assert_close(gui.get_property("debugLogListView", "contentSpacing"), 2,
                 "debug log entry command/content spacing")
    assert_close(gui.get_property("debugLogListView", "lineNumberWidth"), 20,
                 "debug log line number slot width")
    assert effective_line_number_width >= 20, (
        "Expected the effective line-number slot to honor its 20px minimum"
    )
    assert_close(gui.get_property("debugLogListView", "fontPixelSize"), 12,
                 "debug log entry font size")
    assert_close(gui.get_property("debugLogListView", "textLineHeight"), 17,
                 "debug log entry line height")
    assert_close(first_row_viewport_y, 10,
                 "first debug log row viewport y")
    assert_close(gui.get_property("debugLogListView_row_0", "spacing"), 10,
                 "first debug log row column gap")
    assert_close(gui.get_property("debugLogListView_entryContent_0", "spacing"), 2,
                 "first debug log entry internal gap")
    assert_close(
        gui.get_property("debugLogListView_lineNumber_0", "width"),
        effective_line_number_width,
        "first debug log line number width",
    )
    first_text_object = (
        "debugLogListView_message_0"
        if first_row_has_command
        else "debugLogListView_commandlessMessage_0"
    )
    assert_close(gui.get_property(first_text_object, "font.pixelSize"), 12,
                 "first debug log message font size")
    print("  PASSED: search row, divider, and log entry geometry match design")


def test_refresh_button(gui, original_count):
    """Clicking refresh reloads the log without dropping existing entries."""
    print("\n── test_refresh_button ───────────────────────────────────────────")
    gui.click("debugLogRefreshButton")
    count = gui.wait_for_property(
        "debugLogListView", "count", lambda c: c >= original_count, timeout_ms=3000
    )
    assert count >= original_count, (
        f"Expected count >= {original_count} after refresh, got {count}"
    )
    print(f"  PASSED: count after refresh = {count}")
    return count


def test_auto_refresh(gui, datadir, current_count):
    """Appending refreshes the capped tail without exceeding its load limit."""
    print("\n── test_auto_refresh ─────────────────────────────────────────────")
    log_path = os.path.join(datadir, "regtest", "debug.log")
    ts = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    marker_body = "test-automation auto-refresh marker"
    marker = f"{ts} {marker_body}"
    with open(log_path, "a", encoding="utf-8") as f:
        f.write(marker + "\n")

    # A capped model can insert the new row and remove one old row without
    # changing count. Filter for the unique marker to observe the actual data
    # update rather than treating row-count growth as the refresh signal.
    gui.set_text("debugLogSearchField", marker_body)
    gui.wait_for_property("debugLogListView", "count", 1, timeout_ms=5000)
    gui.wait_for_property(
        "debugLogListView_commandlessMessage_0",
        "text",
        marker_body,
        timeout_ms=3000,
    )

    gui.wait_for_property("debugLogListView_command_0", "visible", False, timeout_ms=3000)
    gui.wait_for_property("debugLogListView_commandlessMessage_0", "visible", True, timeout_ms=3000)
    gui.wait_for_property("debugLogListView_message_0", "visible", False, timeout_ms=3000)
    gui.invoke("debugLogListView_commandlessMessage_0", "selectAll")
    gui.wait_for_property(
        "debugLogListView_commandlessMessage_0",
        "selectedText",
        marker_body,
        timeout_ms=3000,
    )
    print("  PASSED: commandless log entry renders inline with the date")

    gui.set_text("debugLogSearchField", "")
    restored_count = gui.wait_for_property(
        "debugLogListView", "count", current_count, timeout_ms=3000
    )
    assert restored_count == INITIAL_LOAD_LIMIT, (
        f"Expected auto-refresh to retain the {INITIAL_LOAD_LIMIT}-row cap, "
        f"got {restored_count}"
    )
    print(
        f"  PASSED: appended entry loaded and count remained capped at "
        f"{restored_count}"
    )
    return restored_count


def test_four_digit_line_numbers_are_not_clipped(gui, datadir, current_count):
    """Verify virtualized offscreen rows and the four-digit number column."""
    print("\n── test_four_digit_line_numbers_are_not_clipped ────────────────")
    target_count = 1000
    needed = max(0, target_count - current_count)
    if needed > 0:
        log_path = os.path.join(datadir, "regtest", "debug.log")
        ts = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
        with open(log_path, "a", encoding="utf-8") as f:
            for i in range(needed):
                f.write(f"{ts} four-digit-line-number marker {i}\n")

    count = gui.wait_for_property(
        "debugLogListView", "count", lambda c: c >= target_count, timeout_ms=5000
    )
    assert count >= target_count, (
        f"Expected at least {target_count} lines after append, got {count}"
    )

    last_index = count - 1
    last_line_number = f"debugLogListView_lineNumber_{last_index}"
    gui.invoke("debugLogListView", "scrollToTop")
    gui.wait_for_property("debugLogListView", "atTop", True, timeout_ms=3000)
    assert not gui.object_exists(last_line_number), (
        f"Expected offscreen row {last_index} not to be instantiated at the top"
    )

    gui.invoke("debugLogListView", "scrollToBottom")
    gui.wait_for_property("debugLogListView", "atBottom", True, timeout_ms=3000)
    gui.wait_for_property(last_line_number, "text", str(count), timeout_ms=3000)
    line_number_width = gui.get_property(last_line_number, "width")
    line_number_implicit_width = gui.get_property(last_line_number, "implicitWidth")
    assert line_number_width >= line_number_implicit_width, (
        f"Expected four-digit line number width {line_number_width} to fit "
        f"implicit width {line_number_implicit_width}"
    )
    assert gui.get_property("debugLogListView", "effectiveLineNumberWidth") > 20, \
        "Expected effective line-number slot to expand beyond the design minimum"
    print("  PASSED: offscreen rows are virtualized and four-digit line numbers fit")


def test_load_more_at_bottom(gui, current_count):
    """The bottom affordance appends older rows without moving the viewport."""
    print("\n── test_load_more_at_bottom ───────────────────────")
    gui.invoke("debugLogListView", "scrollToBottom")
    gui.wait_for_property("debugLogListView", "atBottom", True, timeout_ms=3000)
    gui.wait_for_property("debugLogLoadMoreButton", "visible", True, timeout_ms=3000)
    content_y_before = gui.get_property("debugLogListView", "contentY")

    gui.click("debugLogLoadMoreButton")
    expanded_count = gui.wait_for_property(
        "debugLogListView", "count", lambda c: c > current_count, timeout_ms=5000
    )
    content_y_after = gui.get_property("debugLogListView", "contentY")
    assert_close(
        content_y_after,
        content_y_before,
        "load-more viewport anchor",
        tolerance=2,
    )
    gui.wait_for_property("debugLogLoadMoreButton", "visible", False, timeout_ms=3000)

    oldest_index = expanded_count - 1
    oldest_message = f"debugLogListView_commandlessMessage_{oldest_index}"
    gui.invoke("debugLogListView", "scrollToBottom")
    gui.wait_for_property(
        oldest_message,
        "text",
        "test-automation seeded-history marker 0",
        timeout_ms=3000,
    )
    print(
        f"  PASSED: loaded {expanded_count - current_count} older rows without "
        "moving the viewport, including the oldest seeded row"
    )
    return expanded_count


def test_close_settings(gui):
    """Clicking Done exits the desktop settings shell."""
    print("\n── test_close_settings ───────────────────────────────────────────")
    gui.click("settingsDoneButton")
    gui.wait_for_page("nodeSettingsButton", timeout_ms=5000)
    print("  PASSED: Done closed node settings")


def test_search_filter(gui, total_count):
    """Typing in the search field filters the list; clearing it restores all entries."""
    print("\n── test_search_filter ────────────────────────────────────────────")

    # "Bitcoin" appears in the startup banner ("Bitcoin Core version ...") so
    # it is guaranteed to match some lines but very likely not all of them.
    gui.set_text("debugLogSearchField", "Bitcoin")
    # Wait for the debounce timer (150 ms) to propagate searchFilter to the model.
    filtered = gui.wait_for_property(
        "debugLogListView", "count", lambda c: c < total_count
    )
    assert 0 < filtered < total_count, (
        f"Expected a non-zero subset of {total_count} lines after filtering for "
        f"'Bitcoin', got {filtered}"
    )
    print(f"  Filtered count ('Bitcoin'): {filtered} / {total_count}")

    # Clear the search — full list should be restored.
    gui.set_text("debugLogSearchField", "")
    restored = gui.wait_for_property(
        "debugLogListView", "count", lambda c: c == total_count
    )
    assert restored == total_count, (
        f"Expected count to restore to {total_count} after clearing filter, "
        f"got {restored}"
    )
    print(f"  Restored count (cleared): {restored}")
    print("  PASSED: search filter works correctly")


# ── Entry point ───────────────────────────────────────────────────────────────

def run_tests():
    harness = DebugLogHarness()
    try:
        harness.start()
        gui = harness.driver

        print("\nNavigating to Debug Log ...")
        navigate_to_debug_log(gui)
        print(f"  -> page: {gui.get_current_page()}")

        test_viewer_visibility(gui)
        total = test_log_has_entries(gui)
        test_search_layout_matches_design(gui)
        test_search_filter(gui, total)
        total = test_auto_refresh(gui, harness.datadir, total)
        total = test_refresh_button(gui, total)
        test_four_digit_line_numbers_are_not_clipped(gui, harness.datadir, total)
        total = test_load_more_at_bottom(gui, total)
        test_close_settings(gui)

        print("\n" + "=" * 50)
        print("All debug log tests PASSED")
        print("=" * 50)

    except Exception as e:
        print(f"\nFAILED: {e}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        if harness.process:
            try:
                harness.process.send_signal(signal.SIGTERM)
                try:
                    stderr_bytes = harness.process.communicate(timeout=5)[1]
                except Exception:
                    harness.process.kill()
                    stderr_bytes = harness.process.communicate()[1]
                if stderr_bytes:
                    print("\n--- GUI stderr ---", file=sys.stderr)
                    print(stderr_bytes.decode("utf-8", errors="replace")[-4000:],
                          file=sys.stderr)
            except Exception:
                pass
        if harness.driver:
            dump_qml_tree(harness.driver)
        sys.exit(1)
    finally:
        harness.stop()


if __name__ == '__main__':
    run_tests()
