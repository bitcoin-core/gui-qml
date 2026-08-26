#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Exercise the redesigned Settings → Debug Log page.

The page shows logs oldest-to-newest with separate Type, Time, and
Message columns. The initial window is capped, live rows arrive at the bottom,
and older history can be prepended on demand.

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


class DebugLogHarness:
    """Launch the GUI node with an onboarded regtest profile."""

    def __init__(self):
        self.gui_binary = find_gui_binary()
        self.tmpdir = tempfile.mkdtemp(prefix="qml_test_debug_log_")
        self.socket_path = os.path.join(self.tmpdir, "test_bridge.sock")
        self.process = None
        self.driver = None
        self.datadir = setup_datadir(self.tmpdir)
        self._seed_debug_log_history()

    def _seed_debug_log_history(self):
        network_dir = os.path.join(self.datadir, "regtest")
        os.makedirs(network_dir, exist_ok=True)
        log_path = os.path.join(network_dir, "debug.log")
        with open(log_path, "w", encoding="utf-8") as log_file:
            for index in range(SEEDED_HISTORY_LINES):
                if index == 0:
                    message = "test-automation oldest-history marker"
                elif index == 1194:
                    message = "[net:warning] test-automation warning marker"
                elif index == 1195:
                    message = "[net] test-automation ordered-history marker older"
                elif index == 1196:
                    message = "[rpc] test-automation ordered-history marker newer"
                elif index == 1197:
                    message = "[net] test-automation network microsecond marker"
                elif index == 1198:
                    message = "[rpc:error] test-automation rpc error marker"
                elif index == 1199:
                    message = "[mempool] test-automation mempool marker"
                else:
                    message = f"test-automation seeded-history marker {index}"
                log_file.write(f"2026-01-01T00:00:00.123456Z {message}\n")

    def start(self):
        env = dict(os.environ)
        env["QT_QPA_PLATFORM"] = "offscreen"
        args = [
            self.gui_binary,
            f"-datadir={self.datadir}",
            f"-test-automation={self.socket_path}",
            "-qml_onboarded=1",
            "-disablewallet",
            "-logtimemicros",
            "-debug",
            "-debugexclude=leveldb",
            "-nolisten",
        ]
        print(f"Starting GUI: {' '.join(args)}")
        self.process = subprocess.Popen(
            args, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
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


def navigate_to_debug_log(gui):
    gui.wait_for_page("nodeRunner", timeout_ms=10000)
    gui.click("nodeSettingsButton")
    gui.wait_for_page("settingsView", timeout_ms=5000)
    gui.wait_for_property("settingsSidebar_debug-log", "visible", True, timeout_ms=5000)
    gui.click("settingsSidebar_debug-log")
    gui.wait_for_page("debugLogView", timeout_ms=5000)


def test_page_structure(gui):
    print("\n── test_page_structure ──────────────────────────────────────────")
    for object_name in (
        "debugLogSettingsHeader",
        "debugLogPageHeading",
        "debugLogToolsRow",
        "debugLogSearchField",
        "debugLogOptionsButton",
        "debugLogTableSectionCard",
        "debugLogListView",
        "debugLogTitlesHeader",
        "debugLogTitlesHeaderDivider",
        "debugLogTableFooter",
        "debugLogTableFooterDivider",
        "debugLogScrollToBottomButton",
    ):
        gui.wait_for_property(object_name, "visible", True, timeout_ms=5000)

    count = gui.wait_for_property(
        "debugLogListView", "count", INITIAL_LOAD_LIMIT, timeout_ms=5000
    )
    assert count == INITIAL_LOAD_LIMIT

    page_width = gui.get_property("debugLogView", "width")
    content_width = gui.get_property("debugLogContentLayout", "width")
    padding = gui.get_property("debugLogView", "contentHorizontalPadding")
    maximum_width = gui.get_property("debugLogView", "maximumContentWidth")
    expected_width = max(0, min(page_width - padding * 2, maximum_width))
    assert_close(content_width, expected_width, "debug log content width")
    assert_close(
        gui.get_property("debugLogContentLayout", "x"),
        (page_width - content_width) / 2,
        "debug log content centering",
    )
    assert_close(gui.get_property("debugLogOptionsButton", "height"), 36, "options")
    assert gui.get_property("debugLogOptionsButton", "iconSource") == "image://images/ellipsis"
    assert_close(gui.get_property("debugLogSearchField", "height"), 40, "search")
    assert_close(gui.get_property("debugLogTitlesHeader", "height"), 44, "table header")
    assert_close(gui.get_property("debugLogTitlesHeaderDivider", "height"), 1, "header divider")
    assert_close(gui.get_property("debugLogTableFooter", "height"), 44, "table footer")
    assert_close(gui.get_property("debugLogTableFooterDivider", "height"), 1, "footer divider")

    gui.invoke("debugLogView", "scrollToTop")
    gui.wait_for_property("debugLogItemRow_0", "visible", True, timeout_ms=3000)
    for suffix in ("TypeIndicator", "Time", "Message"):
        gui.wait_for_property(f"debugLogItemRow_0{suffix}", "visible", True, timeout_ms=3000)
    assert gui.get_property("debugLogItemRow_0", "height") >= 48
    print("  PASSED: redesigned table and capped initial window are visible")
    return count


def test_structured_columns_and_filters(gui):
    print("\n── test_structured_columns_and_filters ─────────────────────────")
    gui.set_text("debugLogSearchField", "network microsecond marker")
    gui.wait_for_property("debugLogListView", "count", 1, timeout_ms=5000)
    timestamp = gui.get_property("debugLogItemRow_0Time", "text")
    assert timestamp == "00:00:00", f"Expected HH:MM:SS time, got {timestamp!r}"
    gui.wait_for_property(
        "debugLogItemRow_0Message", "text",
        "test-automation network microsecond marker", timeout_ms=3000,
    )
    gui.invoke("debugLogItemRow_0Message", "selectAll")
    gui.wait_for_property(
        "debugLogItemRow_0Message", "selectedText",
        "test-automation network microsecond marker", timeout_ms=3000,
    )

    gui.set_text("debugLogSearchField", "rpc error marker")
    gui.wait_for_property("debugLogListView", "count", 1, timeout_ms=5000)
    gui.click("debugLogOptionsButton")
    gui.wait_for_property("debugLogOptionsMenu", "opened", True, timeout_ms=3000)
    gui.click("debugLogFilterWarningsAndErrors")
    gui.wait_for_property("debugLogListView", "count", 1, timeout_ms=3000)

    gui.set_text("debugLogSearchField", "warning marker")
    gui.wait_for_property("debugLogListView", "count", 1, timeout_ms=5000)
    gui.click("debugLogOptionsButton")
    gui.wait_for_property("debugLogOptionsMenu", "opened", True, timeout_ms=3000)
    gui.click("debugLogFilterAllMessages")
    gui.set_text("debugLogSearchField", "")
    gui.wait_for_property("debugLogListView", "count", INITIAL_LOAD_LIMIT, timeout_ms=5000)
    print("  PASSED: type, HH:MM:SS time, message, and warning/error filter work")


def test_chronological_order(gui):
    print("\n── test_chronological_order ─────────────────────────────────────")
    gui.set_text("debugLogSearchField", "ordered-history marker")
    gui.wait_for_property("debugLogListView", "count", 2, timeout_ms=5000)
    gui.invoke("debugLogView", "scrollToTop")
    gui.wait_for_property(
        "debugLogItemRow_0Message", "text",
        "test-automation ordered-history marker older", timeout_ms=3000,
    )
    gui.wait_for_property(
        "debugLogItemRow_1Message", "text",
        "test-automation ordered-history marker newer", timeout_ms=3000,
    )
    gui.set_text("debugLogSearchField", "")
    gui.wait_for_property("debugLogListView", "count", INITIAL_LOAD_LIMIT, timeout_ms=5000)
    print("  PASSED: rows are oldest-to-newest")


def test_live_append(gui, datadir):
    print("\n── test_live_append ─────────────────────────────────────────────")
    marker_body = "test-automation live network marker"
    gui.set_text("debugLogSearchField", marker_body)
    gui.wait_for_property("debugLogListView", "count", 0, timeout_ms=5000)

    timestamp = datetime.datetime.now(datetime.timezone.utc).strftime(
        "%Y-%m-%dT%H:%M:%S.654321Z"
    )
    log_path = os.path.join(datadir, "regtest", "debug.log")
    with open(log_path, "a", encoding="utf-8") as log_file:
        log_file.write(f"{timestamp} [net] {marker_body}\n")

    gui.wait_for_property("debugLogListView", "count", 1, timeout_ms=5000)
    gui.wait_for_property("debugLogItemRow_0Message", "text", marker_body, timeout_ms=3000)
    gui.set_text("debugLogSearchField", "")
    gui.wait_for_property("debugLogListView", "count", INITIAL_LOAD_LIMIT, timeout_ms=5000)
    print("  PASSED: live rows arrive without exceeding the loaded-row cap")


def test_load_older(gui):
    print("\n── test_load_older ──────────────────────────────────────────────")
    gui.invoke("debugLogView", "scrollToTop")
    gui.wait_for_property("debugLogLoadMoreButton", "visible", True, timeout_ms=3000)
    gui.click("debugLogLoadMoreButton")
    expanded = gui.wait_for_property(
        "debugLogListView", "count", lambda count: count > INITIAL_LOAD_LIMIT,
        timeout_ms=5000,
    )

    gui.set_text("debugLogSearchField", "oldest-history marker")
    gui.wait_for_property("debugLogListView", "count", 1, timeout_ms=5000)
    gui.wait_for_property(
        "debugLogItemRow_0Message", "text",
        "test-automation oldest-history marker", timeout_ms=3000,
    )
    assert expanded >= SEEDED_HISTORY_LINES
    print(f"  PASSED: older history was prepended ({expanded} rows loaded)")


def test_close_settings(gui):
    print("\n── test_close_settings ──────────────────────────────────────────")
    gui.click("settingsDoneButton")
    gui.wait_for_page("nodeSettingsButton", timeout_ms=5000)
    print("  PASSED: Done closed node settings")


def run_tests():
    harness = DebugLogHarness()
    try:
        harness.start()
        gui = harness.driver
        print("\nNavigating to Debug Log ...")
        navigate_to_debug_log(gui)
        print(f"  -> page: {gui.get_current_page()}")

        test_page_structure(gui)
        test_structured_columns_and_filters(gui)
        test_chronological_order(gui)
        test_live_append(gui, harness.datadir)
        test_load_older(gui)
        test_close_settings(gui)

        print("\n" + "=" * 50)
        print("All debug log tests PASSED")
        print("=" * 50)

    except Exception as error:
        print(f"\nFAILED: {error}", file=sys.stderr)
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
                    print(
                        stderr_bytes.decode("utf-8", errors="replace")[-4000:],
                        file=sys.stderr,
                    )
            except Exception:
                pass
        if harness.driver:
            dump_qml_tree(harness.driver)
        sys.exit(1)
    finally:
        harness.stop()


if __name__ == "__main__":
    run_tests()
