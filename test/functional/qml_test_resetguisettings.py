#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Exercise reset GUI settings semantics across onboarding restarts."""

import json
import os
import shutil
import sys
import tempfile
import time

from qml_test_harness import (
    QmlTestHarness,
    dump_qml_tree,
    parse_args,
)


def click_to_storage_location(gui):
    gui.wait_for_page("onboardingCover", timeout_ms=10000)
    for button, expected_page in [
        ("onboardingCoverButton", "onboardingStrengthen"),
        ("onboardingStrengthenButton", "onboardingBlockclock"),
        ("onboardingBlockclockButton", "onboardingStorageLocation"),
    ]:
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
    gui.click("onboardingStorageLocationButton")
    gui.wait_for_page("onboardingStorageAmount", timeout_ms=5000)
    gui.wait_for_property("onboardingStorageAmountButton", "enabled", True, timeout_ms=10000)
    gui.click("onboardingStorageAmountButton")
    gui.wait_for_page("onboardingConnection", timeout_ms=5000)


def complete_current_onboarding(harness):
    gui = harness.driver
    click_to_storage_location(gui)
    click_to_connection(gui)
    gui.click("onboardingConnectionButton")
    harness.wait_for_main_window_reconnect()


def open_connection_settings(gui):
    gui.click("connectionSettingsButton")
    gui.wait_for_page("gotoProxy", timeout_ms=5000)


def open_proxy_settings(gui):
    gui.click("gotoProxy")
    gui.wait_for_page("settingsProxy", timeout_ms=5000)


def close_proxy_settings(gui):
    gui.wait_for_property("settingsProxyDone", "enabled", True, timeout_ms=2000)
    gui.click("settingsProxyDone")
    gui.wait_for_page("gotoProxy", timeout_ms=5000)


def close_connection_settings(gui):
    gui.click("connectionSettingsDoneButton")
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


def qsettings_path(config_home, app_name="BitcoinCore-App-regtest"):
    org_dir = "bitcoincore.org" if sys.platform == "darwin" else "BitcoinCore"
    return os.path.join(config_home, org_dir, f"{app_name}.ini")


def read_qsettings(path):
    with open(path, encoding="utf8") as settings_file:
        return settings_file.read()


def qsettings_values(path):
    values = {}
    for line in read_qsettings(path).splitlines():
        if not line or line.startswith("["):
            continue
        key, value = line.split("=", 1)
        values[key] = value
    return values


def read_qsettings_for_datadir(config_home, datadir):
    settings_path = qsettings_path(config_home)
    settings_text = read_qsettings(settings_path)
    assert f"strDataDir={datadir}" in settings_text, settings_text
    return settings_text


def seed_qsettings(
    config_home,
    datadir,
    *,
    app_name="BitcoinCore-App-regtest",
    reset=True,
    sentinel=None,
):
    settings_path = qsettings_path(config_home, app_name)
    settings_dir = os.path.dirname(settings_path)
    os.makedirs(settings_dir, exist_ok=True)
    with open(settings_path, "w", encoding="utf8") as settings_file:
        settings_file.write("[General]\n")
        settings_file.write(f"strDataDir={datadir}\n")
        settings_file.write("language=es\n")
        settings_file.write(f"fReset={'true' if reset else 'false'}\n")
        settings_file.write("dark=true\n")
        settings_file.write("blockclocksize=0.4166666666666667\n")
        if sentinel is not None:
            settings_file.write(f"resetSentinel={sentinel}\n")
    return settings_path


def navigate_to_display_settings(gui):
    gui.wait_for_page("nodeSettingsButton", timeout_ms=30000)
    gui.click("nodeSettingsButton")
    gui.wait_for_property("settings_display", "visible", True, timeout_ms=5000)
    gui.click("settings_display")
    gui.wait_for_page("gotoLanguage", timeout_ms=5000)


def run_malformed_settings_reset_recovers(tmpdir):
    case_tmpdir = os.path.join(tmpdir, "malformed-settings-reset")
    os.makedirs(case_tmpdir, exist_ok=True)
    harness = QmlTestHarness(
        tmpdir=case_tmpdir,
        reset_settings=True,
        extra_args=["-regtest", "-disablewallet"],
    )
    network_dir = os.path.join(harness.datadir, "regtest")
    os.makedirs(network_dir, exist_ok=True)
    settings_path = os.path.join(network_dir, "settings.json")
    with open(settings_path, "w", encoding="utf8") as settings_file:
        settings_file.write("{not valid json")

    gui = None
    try:
        harness.start()
        gui = harness.driver
        complete_current_onboarding(harness)
        settings = load_settings(harness.datadir)
        assert settings.get("qml_onboarded") is True, settings
    except Exception:
        if gui is not None:
            dump_qml_tree(gui)
        raise
    finally:
        harness.stop(cleanup=False)


def run_untouched_legacy_store_does_not_block(tmpdir):
    case_tmpdir = os.path.join(tmpdir, "untouched-legacy-store")
    os.makedirs(case_tmpdir, exist_ok=True)
    harness = QmlTestHarness(
        tmpdir=case_tmpdir,
        extra_args=["-disablewallet"],
    )
    legacy_path = os.path.join(
        harness.config_home,
        "Bitcoin",
        "Bitcoin-Qt-regtest.ini",
    )
    os.makedirs(legacy_path, exist_ok=True)
    marker_path = os.path.join(legacy_path, "untouched")
    with open(marker_path, "w", encoding="utf8") as marker_file:
        marker_file.write("keep")

    gui = None
    try:
        harness.start()
        gui = harness.driver
        gui.wait_for_page("nodeSettingsButton", timeout_ms=30000)
        assert read_qsettings(marker_path) == "keep"
    except Exception:
        if gui is not None:
            dump_qml_tree(gui)
        raise
    finally:
        harness.stop(cleanup=False)


def run_explicit_false_reset_preserves_qsettings(tmpdir):
    name = "command-line-zero"
    case_tmpdir = os.path.join(tmpdir, name)
    os.makedirs(case_tmpdir, exist_ok=True)
    harness = QmlTestHarness(
        tmpdir=case_tmpdir,
        extra_args=[
            "-regtest",
            "-disablewallet",
            "-qml_onboarded=1",
            "-resetguisettings=0",
        ],
    )
    settings_path = seed_qsettings(
        harness.config_home,
        harness.datadir,
        reset=False,
        sentinel=name,
    )
    gui = None
    try:
        harness.start()
        gui = harness.driver
        gui.wait_for_page("nodeSettingsButton", timeout_ms=30000)
        harness.stop(cleanup=False)
        settings_text = read_qsettings(settings_path)
        assert f"strDataDir={harness.datadir}" in settings_text, settings_text
        assert "language=es" in settings_text, settings_text
        assert "fReset=false" in settings_text, settings_text
        assert f"resetSentinel={name}" in settings_text, settings_text
    except Exception:
        if gui is not None:
            dump_qml_tree(gui)
        raise
    finally:
        harness.stop(cleanup=False)


def run_resolved_network_reset_flag_shows_onboarding(tmpdir):
    case_tmpdir = os.path.join(tmpdir, "active-reset")
    os.makedirs(case_tmpdir, exist_ok=True)
    seed_harness = QmlTestHarness(
        tmpdir=case_tmpdir,
        extra_args=["-disablewallet"],
    )
    active_qsettings_path = seed_qsettings(
        seed_harness.config_home,
        seed_harness.datadir,
        reset=True,
        sentinel="active-profile",
    )
    bootstrap_qsettings_path = seed_qsettings(
        seed_harness.config_home,
        seed_harness.datadir,
        app_name="BitcoinCore-App",
        reset=False,
        sentinel="bootstrap-profile",
    )
    original_active_qsettings = qsettings_values(active_qsettings_path)
    original_bootstrap_qsettings = qsettings_values(bootstrap_qsettings_path)

    harness = QmlTestHarness(
        datadir=seed_harness.datadir,
        use_datadir_arg=False,
        extra_args=["-disablewallet"],
    )
    gui = None
    try:
        harness.start()
        gui = harness.driver
        gui.wait_for_page("onboardingCover", timeout_ms=10000)
        gui.close_window()
        return_code = harness.process.wait(timeout=10)
        assert return_code == 0, harness.process_output()
        gui = None

        active_qsettings = qsettings_values(active_qsettings_path)
        assert active_qsettings == original_active_qsettings, active_qsettings
        bootstrap_qsettings = qsettings_values(bootstrap_qsettings_path)
        assert bootstrap_qsettings == original_bootstrap_qsettings, bootstrap_qsettings
    except Exception:
        if gui is not None:
            dump_qml_tree(gui)
        raise
    finally:
        harness.stop(cleanup=False)


def run_config_only_reset_resets_final_profile(tmpdir):
    case_tmpdir = os.path.join(tmpdir, "config-only-reset")
    os.makedirs(case_tmpdir, exist_ok=True)
    harness = QmlTestHarness(
        tmpdir=case_tmpdir,
        extra_args=["-disablewallet"],
    )

    config_path = os.path.join(harness.datadir, "bitcoin.conf")
    with open(config_path, encoding="utf8") as config_file:
        config = config_file.read()
    with open(config_path, "w", encoding="utf8") as config_file:
        config_file.write("resetguisettings=1\n")
        config_file.write(config)

    network_dir = os.path.join(harness.datadir, "regtest")
    os.makedirs(network_dir, exist_ok=True)
    settings_path = os.path.join(network_dir, "settings.json")
    original_settings = {
        "qml_onboarded": True,
        "server": True,
        "proxy": "10.0.0.1:9050",
    }
    with open(settings_path, "w", encoding="utf8") as settings_file:
        json.dump(original_settings, settings_file)

    active_qsettings_path = seed_qsettings(
        harness.config_home,
        harness.datadir,
        reset=False,
        sentinel="active-profile",
    )
    bootstrap_qsettings_path = seed_qsettings(
        harness.config_home,
        harness.datadir,
        app_name="BitcoinCore-App",
        reset=False,
        sentinel="bootstrap-profile",
    )
    original_active_qsettings = qsettings_values(active_qsettings_path)
    original_bootstrap_qsettings = qsettings_values(bootstrap_qsettings_path)

    gui = None
    launched_harnesses = [harness]
    try:
        # Config-owned reset must be visible before InitConfig, and closing the
        # onboarding window must not mutate either settings store.
        harness.start()
        gui = harness.driver
        gui.wait_for_page("onboardingCover", timeout_ms=10000)
        assert gui.get_text("onboardingCoverButton") == "Iniciar"
        gui.close_window()
        return_code = harness.process.wait(timeout=10)
        assert return_code == 0, harness.process_output()
        harness.stop(cleanup=False)
        gui = None

        active_settings = qsettings_values(active_qsettings_path)
        assert active_settings == original_active_qsettings, active_settings

        bootstrap_settings = qsettings_values(bootstrap_qsettings_path)
        assert bootstrap_settings == original_bootstrap_qsettings, bootstrap_settings

        with open(settings_path, encoding="utf8") as settings_file:
            assert json.load(settings_file) == original_settings

        settings_backup_path = settings_path + ".bak"
        gui_backup_path = os.path.join(network_dir, "guisettings.ini.bak")
        assert not os.path.exists(settings_backup_path)
        assert not os.path.exists(gui_backup_path)

        # Completing onboarding applies the reset to the resolved regtest
        # profile, preserves the bootstrap datadir, and clears both fReset
        # buckets.
        first_completion = QmlTestHarness(
            datadir=harness.datadir,
            use_datadir_arg=False,
            extra_args=["-disablewallet"],
        )
        launched_harnesses.append(first_completion)
        first_completion.start()
        gui = first_completion.driver
        complete_current_onboarding(first_completion)
        first_completion.stop(cleanup=False)
        gui = None

        active_settings = read_qsettings(active_qsettings_path)
        assert f"strDataDir={harness.datadir}" in active_settings, active_settings
        assert "language=" not in active_settings, active_settings
        assert "resetSentinel=" not in active_settings, active_settings
        assert "fReset=false" in active_settings, active_settings

        bootstrap_settings = read_qsettings(bootstrap_qsettings_path)
        assert f"strDataDir={harness.datadir}" in bootstrap_settings, bootstrap_settings
        assert "language=es" in bootstrap_settings, bootstrap_settings
        assert "resetSentinel=bootstrap-profile" in bootstrap_settings, bootstrap_settings
        assert "fReset=false" in bootstrap_settings, bootstrap_settings

        with open(settings_path, encoding="utf8") as settings_file:
            reset_settings = json.load(settings_file)
        assert reset_settings.get("qml_onboarded") is True, reset_settings
        assert "server" not in reset_settings, reset_settings
        assert "proxy" not in reset_settings, reset_settings

        with open(settings_backup_path, encoding="utf8") as settings_backup_file:
            settings_backup = json.load(settings_backup_file)
        for key, value in original_settings.items():
            assert settings_backup.get(key) == value, settings_backup

        gui_backup = read_qsettings(gui_backup_path)
        assert "language=es" in gui_backup, gui_backup
        assert "resetSentinel=active-profile" in gui_backup, gui_backup
        assert "fReset=false" in gui_backup, gui_backup

        # A persistent config value must show onboarding on every launch, not
        # alternate with a silent reset. Cancelling that launch is still
        # non-mutating.
        settings_before_restart = load_settings(harness.datadir)
        active_before_restart = qsettings_values(active_qsettings_path)
        bootstrap_before_restart = qsettings_values(bootstrap_qsettings_path)
        restart = QmlTestHarness(
            datadir=harness.datadir,
            use_datadir_arg=False,
            extra_args=["-disablewallet"],
        )
        launched_harnesses.append(restart)
        restart.start()
        gui = restart.driver
        gui.wait_for_page("onboardingCover", timeout_ms=10000)
        gui.close_window()
        return_code = restart.process.wait(timeout=10)
        assert return_code == 0, restart.process_output()
        gui = None

        assert qsettings_values(active_qsettings_path) == active_before_restart
        assert qsettings_values(bootstrap_qsettings_path) == bootstrap_before_restart
        assert load_settings(harness.datadir) == settings_before_restart
    except Exception:
        if gui is not None:
            dump_qml_tree(gui)
        raise
    finally:
        for launched_harness in launched_harnesses:
            launched_harness.stop(cleanup=False)

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
        gui.wait_for_property("proxyAddressInput", "validInput", True, timeout_ms=2000)
        set_switch(gui, "torEnableSwitch", True)
        gui.set_text("torAddressInput", "127.0.0.1:9150")
        gui.wait_for_property("torAddressInput", "validInput", True, timeout_ms=2000)
        close_proxy_settings(gui)
        close_connection_settings(gui)

        gui.click("onboardingConnectionButton")
        harness.wait_for_main_window_reconnect()
        navigate_to_display_settings(gui)
        assert gui.get_property("gotoLanguage", "header") == "Language"

        settings_text = read_qsettings_for_datadir(harness.config_home, custom_datadir)
        assert "language=es" not in settings_text, settings_text
        assert "language=" not in settings_text, settings_text
        assert "fReset=false" in settings_text, settings_text

        settings = load_settings(custom_datadir)
        assert "listen" not in settings, settings
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


def run_tests():
    args = parse_args()
    if args.socket_path:
        raise RuntimeError("qml_test_resetguisettings.py must launch the app itself")

    tmpdir = tempfile.mkdtemp(prefix="qml_")
    custom_datadir = os.path.join(tmpdir, "custom-data-dir")
    os.makedirs(custom_datadir, exist_ok=True)
    try:
        run_malformed_settings_reset_recovers(tmpdir)
        run_untouched_legacy_store_does_not_block(tmpdir)
        run_explicit_false_reset_preserves_qsettings(tmpdir)
        run_resolved_network_reset_flag_shows_onboarding(tmpdir)
        run_config_only_reset_resets_final_profile(tmpdir)
        run_first_reset_onboarding(tmpdir, custom_datadir)
        run_second_reset_onboarding(tmpdir, custom_datadir)
        print("\n" + "=" * 50)
        print("All tests PASSED")
        print("=" * 50)
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)


if __name__ == "__main__":
    try:
        run_tests()
    except Exception as e:
        print(f"\nFAILED: {e}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        sys.exit(1)
