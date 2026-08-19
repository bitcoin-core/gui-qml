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
    complete_onboarding,
    dump_qml_tree,
    parse_args,
    qsettings_sandbox_args,
    setup_datadir,
)
from qml_driver import QmlDriver, QmlDriverError


def navigate_to_window_behavior(gui):
    """Open Settings then navigate to the Window Behavior page."""
    gui.click("nodeSettingsButton")
    # Wait for the settings list with the Window Behavior entry.
    gui.wait_for_page("settingsSidebar_window-behavior", timeout_ms=5000)
    gui.click("settingsSidebar_window-behavior")
    gui.wait_for_page("settingsv2WindowBehaviorSettingsPage", timeout_ms=5000)


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
        settings_args = qsettings_sandbox_args(env, os.path.join(tmpdir, "config"))

        gui_args = [
            gui_binary,
            f"-datadir={datadir}",
            f"-test-automation={socket_path}",
        ] + settings_args + [
            "-disablewallet",
            "-qml_onboarded=1",
            "-logtimemicros",
            "-debug",
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
        # ── Runtime tests start from the onboarded node shell ─────────────────
        print("Ensuring onboarding is not visible ...")
        complete_onboarding(gui)

        # With wallet disabled the app transitions to NodeRunner.
        # The gear button objectName "nodeSettingsButton" appears immediately,
        # before full node initialization completes.
        gui.wait_for_page("nodeSettingsButton", timeout_ms=15000)
        print("Reached post-onboarding (NodeRunner) screen.")

        # ── Test 1: Navigation to Window Behavior page ────────────────────────
        print("Test 1: Navigate to Settings → Window Behavior ...")
        navigate_to_window_behavior(gui)
        print("  -> navigated to Window Behavior  ✓")

        # ── Test 2: Required controls are present ─────────────────────────────
        print("Test 2: Verify expected controls exist on the page ...")
        required_controls = [
            "settingsv2ShowTrayIconSwitch",
            "settingsv2MinimizeToTraySwitch",
            "settingsv2MinimizeOnCloseSwitch",
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
            "settingsv2ShowTrayIconSwitch": True,
            "settingsv2MinimizeToTraySwitch": False,
            "settingsv2MinimizeOnCloseSwitch": False,
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
        # Run the toggle tests while only one Window Behavior page instance is in
        # the StackView (before the back/re-open cycle), so objectName lookups
        # are unambiguous.
        #
        # Note: The cascade state-machine (showTrayIcon off → minimizeToTray
        # disabled) is already covered by the C++ unit tests in
        # test_desktopwindowbehaviormodel.cpp. Here we only verify that the
        # toggle round-trip works correctly via the UI on the offscreen backend.
        print("Test 4: Show tray icon toggles off and the model reflects the change ...")
        gui.click("settingsv2ShowTrayIconSwitch")
        gui.wait_for_property("settingsv2ShowTrayIconSwitch", "checked", False, timeout_ms=2000)
        print("  -> Show tray icon clicked off  ✓")

        print("Test 5: Show tray icon toggles back on ...")
        gui.click("settingsv2ShowTrayIconSwitch")
        gui.wait_for_property("settingsv2ShowTrayIconSwitch", "checked", True, timeout_ms=2000)
        print("  -> Show tray icon restored to on  ✓")

        # ── Test 6: Sidebar navigation after interaction ──────────────────────
        print("Test 6: Sidebar navigation still works after interacting with Window Behavior ...")
        gui.click("settingsSidebar_about")
        gui.wait_for_page("settingsv2AboutSettingsPage", timeout_ms=5000)
        print("  -> switched to About section  ✓")

        # ── Test 7: Re-open page (round-trip) ─────────────────────────────────
        print("Test 7: Re-open Window Behavior page (round-trip) ...")
        gui.click("settingsSidebar_window-behavior")
        gui.wait_for_page("settingsv2WindowBehaviorSettingsPage", timeout_ms=5000)
        print("  -> re-opened Window Behavior  ✓")

        # ── Test 8: Close with minimizeOnClose keeps app alive ────────────────
        print("Test 8: Enable minimizeOnClose, close window, verify app survives ...")
        gui.click("settingsv2MinimizeOnCloseSwitch")
        gui.wait_for_property("settingsv2MinimizeOnCloseSwitch", "checked", True, timeout_ms=2000)
        gui.close_window()
        # If minimizeOnClose works, the close event is intercepted and the
        # app stays alive. Verify we can still communicate with the bridge.
        ctx = gui.get_context_property("nodeModel")
        assert ctx is not None, "App shut down after close_window despite minimizeOnClose"
        print("  -> app survived close_window with minimizeOnClose  ✓")

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
