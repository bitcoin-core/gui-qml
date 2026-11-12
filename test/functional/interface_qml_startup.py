#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Bridge, onboarding, configuration sources and GUI reset process boundaries."""

import json
import os
import tempfile
import threading
import time
from pathlib import Path
from qml_driver import QmlDriverError
from qml_process_checks import check_gui_exit
from qml_scenarios import (
    gui_process,
    run_scenarios,
    scenario,
)
from qml_test_harness import (
    QmlTestHarness,
    assert_node_shell_visible,
    click_onboarding_button,
    complete_preinit_onboarding,
    dump_qml_tree,
    walk_onboarding_to_connection,
)


def bridge_smoke():
    with gui_process(extra_args=["-disablewallet"]) as harness:
        gui = harness.driver
        objects = gui.list_objects()
        assert isinstance(objects, list)
        assert "mainPageStack" in {obj["objectName"] for obj in objects}
        assert gui.get_current_page()
        assert gui.get_property("mainPageStack", "objectName") == "mainPageStack"
        for action, messages in (
            (lambda: gui.click("nonExistentObject12345"), ("not found",)),
            (lambda: gui.get_property("mainPageStack", "completelyFakeProperty999"), ("not found", "error")),
            (lambda: gui.wait_for_page("pageDoesNotExist999", timeout_ms=500), ("timed out",)),
        ):
            try:
                action()
            except QmlDriverError as error:
                assert any(message in str(error).lower() for message in messages), str(error)
            else:
                raise AssertionError("The bridge must report an invalid request")


def onboarding_smoke():
    with gui_process(reset_settings=True, start_onboarded=False,
                     use_datadir_arg=False, extra_args=["-regtest", "-disablewallet"]) as harness:
        walk_onboarding_to_connection(harness.driver)
        click_onboarding_button(harness.driver, "onboardingConnectionButton")
        gui = harness.wait_for_main_window_reconnect()
        assert_node_shell_visible(gui)


def boot_mode(extra_args, config_disabled=False, settings_disabled=False, wallet_enabled=False):
    harness = QmlTestHarness(extra_args=extra_args)
    if config_disabled:
        config = Path(harness.datadir) / "bitcoin.conf"
        config.write_text("disablewallet=1\n" + config.read_text(encoding="utf8"), encoding="utf8")
    if settings_disabled:
        settings = Path(harness.datadir) / "regtest/settings.json"
        settings.parent.mkdir(parents=True, exist_ok=True)
        settings.write_text(json.dumps({"disablewallet": True}), encoding="utf8")
    try:
        harness.start()
        gui = harness.driver
        if wallet_enabled:
            gui.wait_for_page("desktopWalletsPage", timeout_ms=30000)
            gui.wait_for_object("walletBadge", timeout_ms=30000)
        else:
            gui.wait_for_page("nodeRunner", timeout_ms=30000)
            gui.click("nodeSettingsButton")
            gui.wait_for_page("settingsView")
            assert not gui.object_exists("settingsSidebar_wallet")
        assert gui.get_context_property("walletController")["exists"] == wallet_enabled
    finally:
        process = harness.process
        harness.stop()
        if process is not None:
            _, stderr = process.communicate()
            check_gui_exit(process.returncode, stderr.decode("utf8", errors="replace"), socket_path=harness.socket_path)


def run_case(action):
    if not hasattr(os, "mkfifo"):
        print("SKIPPED: blocking config-read scenarios require POSIX FIFOs")
        return 77
    harness = QmlTestHarness(extra_args=["-regtest", "-disablewallet", "-qml_onboarded=0"])
    release = threading.Event()
    reading = threading.Event()
    writer_errors = []
    writer = None
    try:
        harness.start()
        gui = harness.driver
        walk_onboarding_to_connection(gui)

        config = Path(harness.datadir) / "bitcoin.conf"
        contents = config.read_text(encoding="utf8")
        restored_config = config.with_suffix(".ready")
        restored_config.write_text(contents, encoding="utf8")
        config.unlink()
        os.mkfifo(config)

        def serve_config():
            try:
                with config.open("w", encoding="utf8") as stream:
                    reading.set()
                    if not release.wait(30):
                        raise TimeoutError("Startup did not release the blocked config read")
                    # The current read uses the FIFO; subsequent startup reads
                    # use the regular file again.
                    restored_config.replace(config)
                    stream.write(contents)
            except Exception as error:
                writer_errors.append(error)

        writer = threading.Thread(target=serve_config, daemon=True)
        writer.start()
        gui.click("onboardingConnectionButton")
        assert reading.wait(10), "Startup never read the selected data directory's config"
        gui.sock.settimeout(5)
        assert gui.get_property("preInitStartupOverlay", "visible")
        assert gui.get_property("preInitStartupBusyIndicator", "running")
        if action == "quit":
            gui.request_quit()
        elif action == "close":
            gui.close_window()
        assert harness.process.poll() is None, "GUI exited before its worker finished"

        release.set()
        writer.join(timeout=10)
        assert not writer.is_alive(), "Config writer did not finish"
        assert not writer_errors, writer_errors

        if action == "finish":
            gui = harness.wait_for_main_window_reconnect()
            assert_node_shell_visible(gui)
            gui.request_quit()
        harness.process.wait(timeout=30)
        _, stderr = harness.process.communicate(timeout=5)
        check_gui_exit(harness.process.returncode, stderr.decode("utf8", errors="replace"), socket_path=harness.socket_path)
    finally:
        release.set()
        if writer is not None:
            writer.join(timeout=5)
        harness.stop()


def click_to_storage_location(gui):
    gui.wait_for_page("onboardingCover", timeout_ms=10000)
    for button, expected_page in [
        ("onboardingCoverButton", "onboardingStrengthen"),
        ("onboardingStrengthenButton", "onboardingBlockchain"),
        ("onboardingBlockchainButton", "onboardingBlockclock"),
        ("onboardingBlockclockButton", "onboardingStorageLocation"),
    ]:
        gui.wait_for_property(button, "enabled", True, timeout_ms=5000)
        gui.click(button)
        gui.wait_for_page(expected_page, timeout_ms=5000)


def select_custom_datadir(gui, datadir):
    gui.wait_for_page("onboardingStorageLocation", timeout_ms=5000)
    assert gui.invoke_property_object(
        "onboardingStorageLocation",
        "settingsModel",
        "selectCustomDataDir",
        [datadir],
    )
    gui.wait_for_property(
        "onboardingStorageLocationButton",
        "enabled",
        True,
        timeout_ms=10000,
    )


def click_to_connection(gui):
    gui.wait_for_property("onboardingStorageLocationButton", "enabled", True, timeout_ms=10000)
    gui.click("onboardingStorageLocationButton")
    gui.wait_for_page("onboardingStorageAmount", timeout_ms=5000)
    gui.wait_for_property("onboardingStorageAmountButton", "enabled", True, timeout_ms=10000)
    gui.click("onboardingStorageAmountButton")
    gui.wait_for_page("onboardingConnection", timeout_ms=5000)


def open_connection_settings(gui):
    gui.click("connectionSettingsButton")
    gui.wait_for_page("proxySettingsRow", timeout_ms=5000)


def open_proxy_settings(gui):
    gui.click("proxySettingsRow")
    gui.wait_for_page("proxySettingsPage", timeout_ms=5000)


def close_proxy_settings(gui):
    gui.wait_for_property("proxySettingsSaveButton", "enabled", True, timeout_ms=2000)
    gui.click("proxySettingsSaveButton")
    gui.wait_for_page("proxySettingsRow", timeout_ms=5000)


def close_connection_settings(gui):
    gui.click("onboardingConnectionSettingsCloseButton")
    gui.wait_for_property("onboardingConnectionSettingsPopup", "visible", False, timeout_ms=5000)
    gui.wait_for_page("onboardingConnectionButton", timeout_ms=5000)


def set_switch(gui, object_name, desired):
    if gui.get_property(object_name, "checked") != desired:
        gui.click(object_name)
        gui.wait_for_property(object_name, "checked", desired, timeout_ms=2000)


def load_settings(datadir):
    settings_path = os.path.join(datadir, "regtest", "settings.json")
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if os.path.exists(settings_path):
            with open(settings_path, encoding="utf8") as settings_file:
                return json.load(settings_file)
        time.sleep(0.1)
    raise AssertionError(f"Timed out waiting for {settings_path}")


def run_window_behavior_reset(hide_tray_icon):
    # Select regtest only through the harness's bitcoin.conf. An explicit
    # -regtest would let the early reset clear the right store and hide a
    # regression in the later reset after the configuration has been read.
    with tempfile.TemporaryDirectory(prefix="qml_reset_window_") as tmpdir:
        for phase in ("save", "reload", "reset"):
            harness = QmlTestHarness(
                tmpdir=tmpdir,
                reset_settings=phase == "reset",
                extra_args=["-disablewallet"],
            )
            try:
                harness.start()
                gui = harness.driver
                if phase == "reset":
                    complete_preinit_onboarding(gui)
                    gui = harness.wait_for_main_window_reconnect()
                gui.wait_for_page("nodeSettingsButton", timeout_ms=30000)
                gui.click("nodeSettingsButton")
                gui.wait_for_page("settingsSidebar_window-behavior", timeout_ms=5000)
                gui.click("settingsSidebar_window-behavior")
                gui.wait_for_page("windowBehaviorSettingsPage", timeout_ms=5000)

                # Write through the GUI so Qt chooses the platform's effective
                # organization and the redirected -test-settings-dir store.
                if phase == "save":
                    set_switch(gui, "showTrayIconSwitch", not hide_tray_icon)
                    set_switch(gui, "minimizeToTraySwitch", not hide_tray_icon)
                    set_switch(gui, "minimizeOnCloseSwitch", True)
                expected = {
                    "showTrayIconSwitch": phase == "reset" or not hide_tray_icon,
                    "minimizeToTraySwitch": phase != "reset" and not hide_tray_icon,
                    "minimizeOnCloseSwitch": phase != "reset",
                }
                for name, checked in expected.items():
                    actual = gui.get_property(name, "checked")
                    assert actual == checked, f"{phase}: {name} is {actual}, expected {checked}"

                gui.request_quit()
                harness.process.wait(timeout=30)
                _, stderr = harness.process.communicate(timeout=5)
                check_gui_exit(harness.process.returncode, stderr.decode("utf8", errors="replace"), socket_path=harness.socket_path)
            finally:
                harness.stop(cleanup=False)


def run_first_reset_onboarding(tmpdir, custom_datadir):
    harness = QmlTestHarness(
        use_datadir_arg=False,
        reset_settings=True,
        tmpdir=tmpdir,
        no_listen_arg=False,
        extra_args=["-regtest", "-disablewallet"],
    )
    gui = None
    try:
        harness.start()
        gui = harness.driver
        click_to_storage_location(gui)
        select_custom_datadir(gui, custom_datadir)
        click_to_connection(gui)
        open_connection_settings(gui)

        set_switch(gui, "listenSwitch", False)
        set_switch(gui, "natpmpSwitch", True)
        set_switch(gui, "serverSwitch", True)
        open_proxy_settings(gui)
        set_switch(gui, "proxyEnableSwitch", True)
        gui.set_text("proxyAddressInput", "10.0.0.1:9050")
        gui.wait_for_property("proxySettingsPage", "draftProxyAddress", "10.0.0.1:9050", timeout_ms=2000)
        gui.wait_for_property("proxySettingsPage", "draftProxyValidationError", "", timeout_ms=2000)
        set_switch(gui, "torEnableSwitch", True)
        gui.set_text("torAddressInput", "127.0.0.1:9150")
        gui.wait_for_property("proxySettingsPage", "draftTorAddress", "127.0.0.1:9150", timeout_ms=2000)
        gui.wait_for_property("proxySettingsPage", "draftTorValidationError", "", timeout_ms=2000)
        close_proxy_settings(gui)
        close_connection_settings(gui)

        gui.click("onboardingConnectionButton")
        harness.wait_for_main_window_reconnect()

        settings = load_settings(custom_datadir)
        assert settings.get("listen") is False, settings
        assert "natpmp" not in settings, settings
        assert settings.get("server") is True, settings
        assert settings.get("proxy") == "10.0.0.1:9050", settings
        assert settings.get("onion") == "127.0.0.1:9150", settings
    except Exception:
        if gui is not None:
            dump_qml_tree(gui)
        raise
    finally:
        harness.stop(cleanup=False)


def run_second_reset_onboarding(tmpdir, custom_datadir):
    harness = QmlTestHarness(
        use_datadir_arg=False,
        reset_settings=True,
        tmpdir=tmpdir,
        no_listen_arg=False,
        extra_args=["-regtest", "-disablewallet"],
    )
    gui = None
    try:
        harness.start()
        gui = harness.driver
        click_to_storage_location(gui)
        select_custom_datadir(gui, custom_datadir)
        click_to_connection(gui)
        open_connection_settings(gui)

        assert gui.get_property("listenSwitch", "checked") is True
        assert gui.get_property("natpmpSwitch", "checked") is True
        assert gui.get_property("serverSwitch", "checked") is False
        open_proxy_settings(gui)
        assert gui.get_property("proxyEnableSwitch", "checked") is False
        assert gui.get_property("torEnableSwitch", "checked") is False
    except Exception:
        if gui is not None:
            dump_qml_tree(gui)
        raise
    finally:
        harness.stop(cleanup=False)


def reset_onboarding_connection():
    with tempfile.TemporaryDirectory(prefix="qml_reset_") as directory:
        datadir = os.path.join(directory, "custom-data-dir")
        os.makedirs(datadir)
        run_first_reset_onboarding(directory, datadir)
        run_second_reset_onboarding(directory, datadir)


if __name__ == "__main__":
    raise SystemExit(run_scenarios('GUI startup and configuration process scenarios', [
        scenario("bridge-protocol", 1, bridge_smoke),
        scenario("onboarding", 2, onboarding_smoke),
        scenario("blocked-startup-finish", 2, run_case, "finish"),
        scenario("blocked-startup-quit", 2, run_case, "quit"),
        scenario("blocked-startup-close", 2, run_case, "close"),
        scenario("disablewallet-cli", 2, boot_mode, ["-disablewallet"]),
        scenario("disablewallet-config", 2, boot_mode, [], config_disabled=True),
        scenario("disablewallet-settings", 2, boot_mode, [], settings_disabled=True),
        scenario("reset-window-with-tray", 2, run_window_behavior_reset, hide_tray_icon=False),
        scenario("reset-window-without-tray", 2, run_window_behavior_reset, hide_tray_icon=True),
        scenario("reset-onboarding-connection", 2, reset_onboarding_connection),
        scenario("wallet-cli-override", 4, boot_mode, ["-disablewallet=0"], config_disabled=True, wallet_enabled=True),
    ]))
