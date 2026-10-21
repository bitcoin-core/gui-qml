#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Grouped integer input must persist the complete value across restart."""

import json
import os
import shutil
from pathlib import Path

from qml_process_checks import check_gui_exit
from qml_test_harness import QmlTestHarness, complete_onboarding, dump_qml_tree


def stop_and_check(harness):
    harness.driver.request_quit()
    _, stderr = harness.process.communicate(timeout=30)
    check_gui_exit(harness.process.returncode, stderr.decode(), socket_path=harness.socket_path)
    harness.stop(cleanup=False)


def open_settings(gui, section):
    gui.click("nodeSettingsButton")
    gui.wait_for_page("settingsSidebar_" + section)
    gui.click("settingsSidebar_" + section)


def type_integer(gui, field, text):
    gui.set_text(field, "")
    gui.type_text(field, text)
    assert gui.get_text(field) == text


def run_tests():
    # Use a UTF-8 locale available in CI; Qt accepts comma groups in the C locale.
    os.environ["LC_ALL"] = "C.UTF-8"
    harness = QmlTestHarness(extra_args=["-disablewallet"])
    tmpdir = harness.tmpdir
    runtime_dir = Path(tmpdir) / "runtime"
    runtime_dir.mkdir(mode=0o700)
    os.environ["XDG_RUNTIME_DIR"] = str(runtime_dir)
    try:
        harness.start()
        gui = harness.driver
        complete_onboarding(gui)
        gui.wait_for_page("nodeSettingsButton", timeout_ms=30000)
        open_settings(gui, "storage")
        if not gui.get_property("pruneSwitch", "checked"):
            gui.click("pruneSwitch")
        type_integer(gui, "pruneTargetInput", "1,000")
        # Navigating away commits through editingFinished on focus loss.
        gui.click("settingsSidebar_mempool")
        type_integer(gui, "mempoolSizeLimitInput", "10,000")
        gui.type_text("mempoolSizeLimitInput", "\n")
        gui.click("settingsSidebar_about")
        gui.click("aboutDeveloperRow")
        gui.wait_for_page("developerDbcacheField")
        type_integer(gui, "developerDbcacheField", "1,000")
        gui.type_text("developerDbcacheField", "\n")
        type_integer(gui, "developerScriptThreadsField", "-1")
        gui.type_text("developerScriptThreadsField", "\n")
        settings_path = Path(harness.datadir) / "regtest/settings.json"
        settings = json.loads(settings_path.read_text())
        assert int(settings["prune"]) == 953674, settings
        assert int(settings["maxmempool"]) == 10000, settings
        assert int(settings["dbcache"]) == 1000, settings
        assert int(settings["par"]) == -1, settings
        stop_and_check(harness)

        harness = QmlTestHarness(extra_args=["-disablewallet"], datadir=harness.datadir)
        harness.start()
        gui = harness.driver
        gui.wait_for_page("nodeSettingsButton", timeout_ms=30000)
        open_settings(gui, "storage")
        assert gui.get_text("pruneTargetInput") == "1000"
        gui.click("settingsSidebar_mempool")
        assert gui.get_text("mempoolSizeLimitInput") == "10000"
        type_integer(gui, "mempoolSizeLimitInput", "1,00")
        gui.type_text("mempoolSizeLimitInput", "\n")
        assert gui.get_property("mempoolSettingsPage", "mempoolSizeError")
        assert json.loads(settings_path.read_text()) == settings
        gui.click("settingsSidebar_about")
        assert json.loads(settings_path.read_text()) == settings
        gui.click("settingsSidebar_mempool")
        # Exercise the model's validation even when text bypasses the validator.
        gui.set_text("mempoolSizeLimitInput", "1,00")
        assert gui.get_property("mempoolSettingsPage", "mempoolSizeError")
        assert json.loads(settings_path.read_text()) == settings
        stop_and_check(harness)
        print("Grouped integer settings persisted across restart PASSED")
    except Exception:
        if harness.driver and harness.process and harness.process.poll() is None:
            dump_qml_tree(harness.driver)
        raise
    finally:
        harness.stop()
        shutil.rmtree(tmpdir, ignore_errors=True)


if __name__ == "__main__":
    run_tests()
