#!/usr/bin/env python3
# Copyright (c) 2021-2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""End-to-end tests for Window Behavior settings (tray icon, minimize behavior).

Navigates to Settings → Window Behavior and verifies that the page is
reachable, the correct controls are present, and toggle state is reflected
in the UI.

Note: desktopPlatform is a compile-time flag (#ifdef __ANDROID__), so it is
true on Linux CI regardless of the QPA platform.  However,
QSystemTrayIcon::isSystemTrayAvailable() returns false with the offscreen
QPA backend, so the tray icon will not visually register.  The switches are
ENABLED (desktopPlatform=true) but the tray won't appear.  This test
therefore focuses on navigation and default-state correctness; the
toggle state-machine is covered by test_desktopwindowbehaviormodel.cpp
(C++ unit tests).  Full interactive testing (minimize, close, tray reopen,
shutdown) requires a headed desktop session.

This test requires:
  - bitcoin-core-app built with -DENABLE_TEST_AUTOMATION=ON
"""

import os
import shutil
import signal
import subprocess
import sys
import tempfile
import time

from qml_test_harness import (
    GUI_STARTUP_TIMEOUT,
    find_gui_binary,
    setup_datadir,
    complete_onboarding,
    dump_qml_tree,
    parse_args,
)
from qml_driver import QmlDriver, QmlDriverError


def navigate_to_window_behavior(gui):
    """Open Settings then navigate to the Window Behavior page."""
    gui.click("nodeSettingsButton")
    # Wait for the settings list with the Window Behavior entry.
    gui.wait_for_page("settingsWindowBehavior", timeout_ms=5000)
    gui.click("settingsWindowBehavior")
    gui.wait_for_page("windowBehaviorPage", timeout_ms=5000)


def run_tests():
    args = parse_args()

    # Support attaching to an already-running instance.
    if args.socket_path:
        gui = QmlDriver(args.socket_path, timeout=GUI_STARTUP_TIMEOUT)
        process = None
        tmpdir = None
    else:
        gui_binary = find_gui_binary()
        tmpdir = tempfile.mkdtemp(prefix="qml_test_tray_")
        datadir = setup_datadir(tmpdir)
        socket_path = os.path.join(tmpdir, "test_bridge.sock")

        env = dict(os.environ)
        env["QT_QPA_PLATFORM"] = "offscreen"

        gui_args = [
            gui_binary,
            f"-datadir={datadir}",
            f"-test-automation={socket_path}",
            "-disablewallet",
            "-resetguisettings",
            "-logtimemicros",
            "-debug",
            "-debugexclude=libevent",
            "-debugexclude=leveldb",
            "-nolisten",
        ]
        print(f"Starting GUI: {' '.join(gui_args)}")
        process = subprocess.Popen(
            gui_args,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        gui = QmlDriver(socket_path, timeout=GUI_STARTUP_TIMEOUT)
        print("QmlDriver connected to test bridge.")

    try:
        # ── Complete onboarding ───────────────────────────────────────────────
        print("Completing onboarding ...")
        complete_onboarding(gui)

        # After onboarding (wallet disabled) the app transitions to NodeRunner.
        # The gear button objectName "nodeSettingsButton" appears immediately,
        # before full node initialization completes.
        gui.wait_for_page("nodeSettingsButton", timeout_ms=15000)
        print("Reached post-onboarding (NodeRunner) screen.")

        # ── Test 1: Navigation to Window Behavior page ────────────────────────
        print("Test 1: Navigate to Settings → Window Behavior ...")
        navigate_to_window_behavior(gui)
        current = gui.get_current_page()
        assert "windowBehaviorPage" in current, (
            f"Expected windowBehaviorPage, got: {current}"
        )
        print(f"  -> current page: {current}  ✓")

        # ── Test 2: Required controls are present ─────────────────────────────
        print("Test 2: Verify expected controls exist on the page ...")
        required_controls = [
            "windowBehaviorBack",
            "showTrayIconSwitch",
            "minimizeToTraySwitch",
            "minimizeOnCloseSwitch",
        ]
        all_objects = gui.list_objects()
        object_names = {o["objectName"] for o in all_objects}
        for name in required_controls:
            assert name in object_names, (
                f"Expected control '{name}' not found in QML tree"
            )
            print(f"  -> {name}  ✓")

        # ── Test 3: Default switch states ─────────────────────────────────────
        # showTrayIcon defaults to true; the others default to false.
        print("Test 3: Verify default switch states ...")
        expected_defaults = {
            "showTrayIconSwitch":    True,   # show tray icon is on by default
            "minimizeToTraySwitch":  False,  # minimize-to-tray is off by default
            "minimizeOnCloseSwitch": False,  # minimize-on-close is off by default
        }
        for switch_name, expected in expected_defaults.items():
            checked = gui.get_property(switch_name, "checked")
            # normalize to bool
            if isinstance(checked, str):
                checked = checked.lower() == "true"
            assert checked == expected, (
                f"{switch_name} expected checked={expected}, got: {checked}"
            )
            print(f"  -> {switch_name}.checked == {str(expected).lower()}  ✓")

        # ── Tests 4–5: showTrayIcon toggle round-trip ─────────────────────────
        # Run the toggle tests while only one windowBehaviorPage instance is in
        # the StackView (before the back/re-open cycle), so objectName lookups
        # are unambiguous.
        #
        # Note: The cascade state-machine (showTrayIcon off → minimizeToTray
        # disabled) is already covered by the C++ unit tests in
        # test_desktopwindowbehaviormodel.cpp. Here we only verify that the
        # toggle round-trip works correctly via the UI on the offscreen backend.
        print("Test 4: showTrayIconSwitch toggles off and the model reflects the change ...")
        gui.click("showTrayIconSwitch")
        gui.wait_for_property("showTrayIconSwitch", "checked", False, timeout_ms=2000)
        print("  -> showTrayIconSwitch clicked off  ✓")

        print("Test 5: showTrayIconSwitch toggles back on ...")
        gui.click("showTrayIconSwitch")
        gui.wait_for_property("showTrayIconSwitch", "checked", True, timeout_ms=2000)
        print("  -> showTrayIconSwitch restored to on  ✓")

        # ── Test 6: Back navigation ───────────────────────────────────────────
        print("Test 6: Back button returns to settings list ...")
        gui.click("windowBehaviorBack")
        gui.wait_for_page("settingsWindowBehavior", timeout_ms=5000)
        current = gui.get_current_page()
        print(f"  -> current page after back: {current}  ✓")

        # ── Test 7: Re-open page (round-trip) ─────────────────────────────────
        print("Test 7: Re-open Window Behavior page (round-trip) ...")
        gui.click("settingsWindowBehavior")
        gui.wait_for_page("windowBehaviorPage", timeout_ms=5000)
        current = gui.get_current_page()
        assert "windowBehaviorPage" in current, (
            f"Could not re-open windowBehaviorPage, got: {current}"
        )
        print(f"  -> current page: {current}  ✓")

        print("\n" + "=" * 50)
        print("All tests PASSED")
        print("=" * 50)

    except Exception as e:
        print(f"\nFAILED: {e}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        if gui is not None:
            dump_qml_tree(gui)
        sys.exit(1)
    finally:
        gui.close()
        if process and process.poll() is None:
            process.send_signal(signal.SIGTERM)
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        if tmpdir:
            shutil.rmtree(tmpdir, ignore_errors=True)


if __name__ == "__main__":
    run_tests()
