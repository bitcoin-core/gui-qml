#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Exercise the real pre-init onboarding flow.

This test intentionally launches bitcoin-core-app without -datadir so
RunPreInitOnboarding() is observable through the pre-init test bridge.
"""

import sys
import json
import os
import shutil
import subprocess
import time
import tempfile

from qml_test_harness import (
    QmlTestHarness,
    assert_node_shell_visible,
    assert_onboarding_wallet_creation_visible,
    assert_wallet_shell_visible,
    complete_preinit_onboarding,
    dump_qml_tree,
    parse_args,
    setup_datadir,
)
from qml_wallet_test_lib import (
    find_bitcoind,
    pick_unused_port,
    rpc_call,
    wait_for_rpc,
    write_datadir,
)


def assert_create_wallet_onboarding_shown(gui, timeout_ms=30000):
    """Assert onboarding routes to wallet creation after post-startup discovery."""
    deadline = time.time() + timeout_ms / 1000
    while time.time() < deadline:
        object_names = {obj.get("objectName") for obj in gui.list_objects()}
        if "createWalletWizard" in object_names:
            break
        if "nodeRunner" in object_names:
            raise AssertionError("Expected wallet onboarding, but nodeRunner is present")
        time.sleep(0.05)
    else:
        raise AssertionError("Timed out waiting for create-wallet onboarding")

    gui.wait_for_object("desktopWalletsPage", timeout_ms=timeout_ms)
    gui.wait_for_property("walletCreationModal", "visible", True, timeout_ms=timeout_ms)
    gui.wait_for_page("walletCreationTypePage", timeout_ms=timeout_ms)
    assert gui.get_property("blockClockTabButton", "checked"), "Node tab should be selected"


def assert_preinit_cover_about_available(gui):
    gui.wait_for_object("onboardingCoverInfoButton", timeout_ms=10000)
    gui.click("onboardingCoverInfoButton")
    gui.wait_for_page("aboutSettingsPage", timeout_ms=10000)
    gui.click("onboardingAboutCloseButton")
    gui.wait_for_page("onboardingCover", timeout_ms=10000)


def finish_preinit_and_reconnect(harness):
    gui = harness.driver
    gui.wait_for_page("onboardingCover", timeout_ms=10000)
    assert_preinit_cover_about_available(gui)
    complete_preinit_onboarding(gui)
    return harness.wait_for_main_window_reconnect()


def click_existing_profile_to_connection(gui, datadir, expect_custom_storage=True):
    gui.wait_for_page("onboardingCover", timeout_ms=10000)
    assert_preinit_cover_about_available(gui)

    gui.click("onboardingCoverButton")
    gui.wait_for_page("onboardingStrengthen", timeout_ms=5000)
    gui.click("onboardingStrengthenButton")
    gui.wait_for_page("onboardingBlockclock", timeout_ms=5000)
    gui.click("onboardingBlockclockButton")
    gui.wait_for_page("onboardingStorageLocation", timeout_ms=5000)
    gui.wait_for_property("storageCustomLocationOption", "checked", True, timeout_ms=5000)
    assert gui.get_property("storageCustomLocationOption", "customDir") == datadir

    gui.click("onboardingStorageLocationButton")
    gui.wait_for_page("onboardingStorageAmount", timeout_ms=5000)
    if expect_custom_storage:
        gui.wait_for_property("storageCustomOption", "checked", True, timeout_ms=5000)
    else:
        assert (
            gui.get_property("storageReduceOption", "checked")
            or gui.get_property("storageFullOption", "checked")
        ), "Expected an existing-profile storage amount selection"

    gui.click("onboardingStorageAmountButton")
    gui.wait_for_page("onboardingConnection", timeout_ms=5000)


def finish_existing_profile_preinit_and_reconnect(harness, datadir, expect_custom_storage=True):
    gui = harness.driver
    click_existing_profile_to_connection(gui, datadir, expect_custom_storage)
    gui.click("onboardingConnectionButton")
    return harness.wait_for_main_window_reconnect()


def read_settings_json(datadir):
    settings_path = os.path.join(datadir, "regtest", "settings.json")
    with open(settings_path, encoding="utf8") as settings_file:
        return json.load(settings_file)


def write_settings_json(datadir, settings):
    settings_dir = os.path.join(datadir, "regtest")
    os.makedirs(settings_dir, exist_ok=True)
    with open(os.path.join(settings_dir, "settings.json"), "w", encoding="utf8") as settings_file:
        json.dump(settings, settings_file)


def assert_qsettings_datadir_not_persisted(config_home):
    for root, _, files in os.walk(config_home):
        for filename in files:
            path = os.path.join(root, filename)
            with open(path, encoding="utf8", errors="ignore") as settings_file:
                assert "strDataDir" not in settings_file.read(), path


def stop_bitcoind(process, rpc_port):
    if process and process.poll() is None:
        try:
            rpc_call(rpc_port, "stop")
        except Exception:
            process.terminate()
        try:
            process.wait(timeout=20)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


def prepare_existing_profile_with_root_wallet(tmpdir, wallet_name):
    datadir = os.path.join(tmpdir, "node0")
    rpc_port = pick_unused_port()
    p2p_port = pick_unused_port()
    write_datadir(datadir, rpc_port, p2p_port)

    network_dir = os.path.join(datadir, "regtest")
    wallets_dir = os.path.join(network_dir, "wallets")
    os.makedirs(network_dir, exist_ok=True)
    assert not os.path.exists(wallets_dir), wallets_dir

    bitcoind = subprocess.Popen(
        [find_bitcoind(), f"-datadir={datadir}"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    try:
        wait_for_rpc(rpc_port)
        assert not os.path.exists(wallets_dir), wallets_dir
        rpc_call(rpc_port, "createwallet", {"wallet_name": wallet_name, "load_on_startup": False})
        assert os.path.isdir(os.path.join(network_dir, wallet_name))
        assert not os.path.exists(wallets_dir), wallets_dir
    finally:
        stop_bitcoind(bitcoind, rpc_port)

    return datadir, rpc_port


def prepare_existing_profile_with_explicit_walletdir(tmpdir, wallet_name):
    datadir = os.path.join(tmpdir, "node0")
    walletdir = os.path.join(tmpdir, "external_wallets")
    rpc_port = pick_unused_port()
    p2p_port = pick_unused_port()
    write_datadir(datadir, rpc_port, p2p_port)
    os.makedirs(walletdir, exist_ok=True)

    bitcoind = subprocess.Popen(
        [find_bitcoind(), f"-datadir={datadir}", f"-walletdir={walletdir}"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    try:
        wait_for_rpc(rpc_port)
        rpc_call(rpc_port, "createwallet", {"wallet_name": wallet_name, "load_on_startup": False})
        assert os.path.isdir(os.path.join(walletdir, wallet_name))
    finally:
        stop_bitcoind(bitcoind, rpc_port)

    return datadir, rpc_port, walletdir


def assert_saved_connection_settings_visible(gui):
    gui.click("connectionSettingsButton")
    gui.wait_for_page("connectionSettingsPage", timeout_ms=5000)
    gui.wait_for_property("listenSwitch", "checked", False, timeout_ms=5000)
    gui.wait_for_property("natpmpSwitch", "checked", True, timeout_ms=5000)
    gui.wait_for_property("serverSwitch", "checked", True, timeout_ms=5000)

    gui.click("proxySettingsRow")
    gui.wait_for_page("proxySettingsPage", timeout_ms=5000)
    gui.wait_for_property("proxyEnableSwitch", "checked", True, timeout_ms=5000)
    gui.wait_for_property("proxyAddressInput", "text", "10.0.0.1:9050", timeout_ms=5000)
    gui.wait_for_property("torEnableSwitch", "checked", True, timeout_ms=5000)
    gui.wait_for_property("torAddressInput", "text", "127.0.0.1:9150", timeout_ms=5000)

    gui.click("proxySettingsBackButton")
    gui.wait_for_page("connectionSettingsPage", timeout_ms=5000)
    gui.click("onboardingConnectionSettingsCloseButton")
    gui.wait_for_page("onboardingConnectionButton", timeout_ms=5000)


def run_wallet_enabled_flow():
    tmpdir = None
    harness = QmlTestHarness(
        use_datadir_arg=False,
        reset_settings=True,
        extra_args=["-regtest"],
    )
    gui = None
    try:
        harness.start()
        gui = finish_preinit_and_reconnect(harness)
        assert_create_wallet_onboarding_shown(gui)
        assert_onboarding_wallet_creation_visible(gui)
        gui.click("onboardingCloseButton")
        assert_wallet_shell_visible(gui)
        assert not gui.object_exists("onboardingStorageLocation"), "Create-wallet Skip must not return to datadir onboarding"

        tmpdir = harness.tmpdir
        harness.stop(cleanup=False)

        restart = QmlTestHarness(
            use_datadir_arg=False,
            reset_settings=False,
            start_onboarded=False,
            tmpdir=tmpdir,
            extra_args=["-regtest"],
        )
        try:
            restart.start()
            gui = restart.driver
            assert_wallet_shell_visible(gui)
            assert not gui.object_exists("createWalletWizard"), "Did not expect create-wallet onboarding on normal restart"
        finally:
            restart.stop(cleanup=False)
    except Exception:
        if gui is not None:
            dump_qml_tree(gui)
        raise
    finally:
        harness.stop(cleanup=False)
        if tmpdir:
            shutil.rmtree(tmpdir, ignore_errors=True)


def run_wallet_disabled_flow():
    harness = QmlTestHarness(
        use_datadir_arg=False,
        reset_settings=True,
        extra_args=["-regtest", "-disablewallet"],
    )
    gui = None
    try:
        harness.start()
        gui = finish_preinit_and_reconnect(harness)
        assert_node_shell_visible(gui)
    except Exception:
        if gui is not None:
            dump_qml_tree(gui)
        raise
    finally:
        harness.stop()


def run_existing_profile_full_onboarding_flow():
    tmpdir = tempfile.mkdtemp(prefix="qml704_", dir="/tmp")
    datadir = setup_datadir(tmpdir)
    with open(os.path.join(datadir, "bitcoin.conf"), "a", encoding="utf8") as conf:
        conf.write("prune=4096\n")
    harness = QmlTestHarness(
        datadir=datadir,
        reset_settings=False,
        start_onboarded=False,
        extra_args=["-regtest", "-disablewallet"],
    )
    gui = None
    try:
        harness.start()
        gui = finish_existing_profile_preinit_and_reconnect(harness, datadir)
        assert_node_shell_visible(gui)

        settings = read_settings_json(datadir)
        assert settings.get("qml_onboarded") is True, settings
        assert "listen" not in settings, settings
        assert "server" not in settings, settings

        harness.stop(cleanup=False)

        restart = QmlTestHarness(
            datadir=datadir,
            reset_settings=False,
            start_onboarded=False,
            extra_args=["-regtest", "-disablewallet"],
        )
        try:
            restart.start()
            gui = restart.driver
            assert_node_shell_visible(gui)
            assert not gui.object_exists("onboardingCover"), "Did not expect pre-init onboarding after qml_onboarded marker"
        finally:
            restart.stop(cleanup=False)
    except Exception:
        if gui is not None:
            dump_qml_tree(gui)
        raise
    finally:
        harness.stop(cleanup=False)
        shutil.rmtree(tmpdir, ignore_errors=True)


def run_configured_datadir_preserves_config_source_flow():
    harness = QmlTestHarness(
        use_datadir_arg=False,
        reset_settings=False,
        start_onboarded=False,
        extra_args=["-regtest", "-disablewallet"],
    )
    tmpdir = harness.tmpdir
    configured_datadir = os.path.join(tmpdir, "configured_node")
    conf_dir = os.path.join(tmpdir, "conf_source")
    conf_path = os.path.join(conf_dir, "bitcoin.conf")
    os.makedirs(configured_datadir, exist_ok=True)
    os.makedirs(conf_dir, exist_ok=True)
    with open(conf_path, "w", encoding="utf8") as conf:
        conf.write("regtest=1\n")
        conf.write(f"datadir={configured_datadir}\n")
        conf.write("[regtest]\n")
        conf.write("server=1\n")
        conf.write("rpcbind=127.0.0.1\n")
        conf.write("rpcallowip=127.0.0.1\n")
        conf.write("discover=0\n")
        conf.write("dnsseed=0\n")
        conf.write("fixedseeds=0\n")
        conf.write("listenonion=0\n")
        conf.write("printtoconsole=0\n")
        conf.write("connect=0\n")
        conf.write("shrinkdebugfile=0\n")
        conf.write("fallbackfee=0.0001\n")
    harness.extra_args.append(f"-conf={conf_path}")

    gui = None
    try:
        harness.start()
        gui = finish_existing_profile_preinit_and_reconnect(harness, configured_datadir, expect_custom_storage=False)
        assert_node_shell_visible(gui)

        settings = read_settings_json(configured_datadir)
        assert settings.get("qml_onboarded") is True, settings
        harness.stop(cleanup=False)
        assert_qsettings_datadir_not_persisted(harness.config_home)
    except Exception:
        if gui is not None:
            dump_qml_tree(gui)
        raise
    finally:
        harness.stop(cleanup=False)
        shutil.rmtree(tmpdir, ignore_errors=True)


def run_existing_profile_preserves_root_wallet_discovery_flow():
    tmpdir = tempfile.mkdtemp(prefix="qml704_wallets_", dir="/tmp")
    wallet_name = "legacyroot"
    datadir = None
    harness = None
    try:
        datadir, rpc_port = prepare_existing_profile_with_root_wallet(tmpdir, wallet_name)
        wallets_dir = os.path.join(datadir, "regtest", "wallets")
        harness = QmlTestHarness(
            datadir=datadir,
            reset_settings=False,
            start_onboarded=False,
            extra_args=["-regtest"],
        )
        harness.start()
        gui = finish_existing_profile_preinit_and_reconnect(harness, datadir, expect_custom_storage=False)
        gui.wait_for_page("desktopWalletsPage", timeout_ms=30000)
        assert_wallet_shell_visible(gui)
        assert not gui.object_exists("createWalletWizard"), "Existing root wallet must not route to create-wallet onboarding"
        assert not os.path.exists(wallets_dir), wallets_dir

        wait_for_rpc(rpc_port)
        wallet_dir_names = [wallet["name"] for wallet in rpc_call(rpc_port, "listwalletdir")["wallets"]]
        assert wallet_name in wallet_dir_names, wallet_dir_names
    except Exception:
        if harness is not None and harness.driver is not None:
            dump_qml_tree(harness.driver)
        raise
    finally:
        if harness is not None:
            harness.stop(cleanup=False)
        shutil.rmtree(tmpdir, ignore_errors=True)


def run_existing_profile_detects_explicit_walletdir_flow():
    tmpdir = tempfile.mkdtemp(prefix="qml704_walletdir_", dir="/tmp")
    wallet_name = "externalwallet"
    harness = None
    try:
        datadir, rpc_port, walletdir = prepare_existing_profile_with_explicit_walletdir(tmpdir, wallet_name)
        harness = QmlTestHarness(
            datadir=datadir,
            reset_settings=False,
            start_onboarded=False,
            extra_args=["-regtest", f"-walletdir={walletdir}"],
        )
        harness.start()
        gui = finish_existing_profile_preinit_and_reconnect(harness, datadir, expect_custom_storage=False)
        gui.wait_for_page("desktopWalletsPage", timeout_ms=30000)
        assert_wallet_shell_visible(gui)
        assert not gui.object_exists("createWalletWizard"), "Existing explicit-walletdir wallet must not route to create-wallet onboarding"

        wait_for_rpc(rpc_port)
        wallet_dir_names = [wallet["name"] for wallet in rpc_call(rpc_port, "listwalletdir")["wallets"]]
        assert wallet_name in wallet_dir_names, wallet_dir_names
    except Exception:
        if harness is not None and harness.driver is not None:
            dump_qml_tree(harness.driver)
        raise
    finally:
        if harness is not None:
            harness.stop(cleanup=False)
        shutil.rmtree(tmpdir, ignore_errors=True)


def run_qml_onboarded_override_reloads_saved_settings_flow():
    tmpdir = tempfile.mkdtemp(prefix="qml704_override_", dir="/tmp")
    datadir = setup_datadir(tmpdir)
    write_settings_json(datadir, {
        "qml_onboarded": True,
        "listen": False,
        "natpmp": True,
        "server": True,
        "proxy": "10.0.0.1:9050",
        "onion": "127.0.0.1:9150",
        "prune": 4096,
    })
    harness = QmlTestHarness(
        datadir=datadir,
        reset_settings=False,
        start_onboarded=False,
        no_listen_arg=False,
        extra_args=["-regtest", "-disablewallet", "-qml_onboarded=0"],
    )
    gui = None
    try:
        harness.start()
        gui = harness.driver
        click_existing_profile_to_connection(gui, datadir)
        assert_saved_connection_settings_visible(gui)

        gui.click("onboardingConnectionButton")
        gui = harness.wait_for_main_window_reconnect()
        assert_node_shell_visible(gui)

        settings = read_settings_json(datadir)
        assert settings.get("qml_onboarded") is True, settings
        assert settings.get("listen") is False, settings
        assert settings.get("natpmp") is True, settings
        assert settings.get("server") is True, settings
        assert settings.get("proxy") == "10.0.0.1:9050", settings
        assert settings.get("onion") == "127.0.0.1:9150", settings
        assert settings.get("prune") == 4096, settings
    except Exception:
        if gui is not None:
            dump_qml_tree(gui)
        raise
    finally:
        harness.stop(cleanup=False)
        shutil.rmtree(tmpdir, ignore_errors=True)


def run_tests():
    args = parse_args()
    if args.socket_path:
        raise RuntimeError("qml_test_preinit_onboarding.py must launch the app itself")

    run_wallet_enabled_flow()
    run_wallet_disabled_flow()
    run_existing_profile_full_onboarding_flow()
    run_configured_datadir_preserves_config_source_flow()
    run_existing_profile_preserves_root_wallet_discovery_flow()
    run_existing_profile_detects_explicit_walletdir_flow()
    run_qml_onboarded_override_reloads_saved_settings_flow()

    print("\n" + "=" * 50)
    print("All tests PASSED")
    print("=" * 50)


if __name__ == "__main__":
    try:
        run_tests()
    except Exception as e:
        print(f"\nFAILED: {e}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        sys.exit(1)
