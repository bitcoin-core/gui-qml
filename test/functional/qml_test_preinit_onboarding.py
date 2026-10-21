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


def click_to_storage_amount(gui, datadir):
    gui.wait_for_page("onboardingCover", timeout_ms=10000)
    # Qt 6.4 offscreen does not apply the window's minimum size.
    for dimension in ("width", "height"):
        minimum = gui.get_property("preInitWindow", "minimum" + dimension.capitalize())
        gui.set_property("preInitWindow", dimension, minimum)
    assert_preinit_cover_about_available(gui)

    gui.wait_for_property("onboardingCoverButton", "enabled", True)
    gui.click("onboardingCoverButton")
    gui.wait_for_page("onboardingStrengthen", timeout_ms=5000)
    gui.click("onboardingStrengthenButton")
    gui.wait_for_page("onboardingBlockchain", timeout_ms=5000)
    gui.click("onboardingBlockchainButton")
    gui.wait_for_page("onboardingBlockclock", timeout_ms=5000)
    gui.click("onboardingBlockclockButton")
    gui.wait_for_page("onboardingStorageLocation", timeout_ms=5000)
    gui.wait_for_property("storageCustomLocationOption", "checked", True, timeout_ms=5000)
    assert gui.get_property("storageCustomLocationOption", "customDir") == datadir

    gui.wait_for_property("onboardingStorageLocationButton", "enabled", True, timeout_ms=10000)
    gui.click("onboardingStorageLocationButton")
    gui.wait_for_page("onboardingStorageAmount", timeout_ms=5000)
    gui.wait_for_property("onboardingChildView", "opacity", 1)


def current_storage_option(gui):
    for name in ("storageReduceOption", "storageFullOption"):
        if gui.get_property(name, "badgeText") == "Your current setting":
            return name
    raise AssertionError("Expected a current-setting badge")


def click_existing_profile_to_connection(gui, datadir, expect_custom_storage=True):
    click_to_storage_amount(gui, datadir)
    current = current_storage_option(gui)
    gui.wait_for_property(current, "checked", True, timeout_ms=5000)
    if expect_custom_storage:
        assert current == "storageReduceOption"
        assert gui.get_property(current, "description") == "Uses about 5GB. For regular wallet use."
    assert not gui.get_property("storageFullOption", "recommended")
    assert not gui.get_property("storageReduceOption", "recommended")

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
        assert "prune" not in settings, settings

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


def run_existing_profile_storage_choices_flow():
    for target, settings_json in ((None, False), (0, False), (1, False), (4096, False), (4096, True)):
        tmpdir = tempfile.mkdtemp(prefix="qml_storage_", dir="/tmp")
        rpc_port = pick_unused_port()
        datadir = os.path.join(tmpdir, "node0")
        write_datadir(datadir, rpc_port, pick_unused_port())
        conf_path = os.path.join(datadir, "bitcoin.conf")
        if settings_json:
            write_settings_json(datadir, {"prune": target})
        elif target is not None:
            with open(conf_path, "a", encoding="utf8") as conf:
                conf.write(f"prune={target}\n")
        with open(conf_path, encoding="utf8") as conf:
            original_config = conf.read()
        harness = QmlTestHarness(
            datadir=datadir,
            reset_settings=False,
            start_onboarded=False,
            extra_args=["-regtest", "-disablewallet"],
        )
        try:
            node = subprocess.Popen(
                [find_bitcoind(), f"-datadir={datadir}", "-listen=0"],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            try:
                wait_for_rpc(rpc_port)
            finally:
                stop_bitcoind(node, rpc_port)
            assert os.path.isdir(os.path.join(datadir, "regtest", "chainstate"))
            harness.start()
            gui = harness.driver
            click_existing_profile_to_connection(gui, datadir, expect_custom_storage=target == 4096)
            gui.click("onboardingWizardBackButton")
            gui.wait_for_page("onboardingStorageAmount")
            current = current_storage_option(gui)
            expected_current = "storageFullOption" if target in (None, 0) else "storageReduceOption"
            assert current == expected_current
            description = gui.get_property(current, "description")
            if target == 1:
                assert gui.get_property(current, "text") == "Manual pruning"
                assert description == "Prune older blocks manually to control storage use."
            else:
                assert gui.get_property(current, "text") == ("Store all data" if target in (None, 0) else "Reduce storage")
            other = "storageReduceOption" if current == "storageFullOption" else "storageFullOption"
            gui.click(other)
            gui.wait_for_property(other, "checked", True)
            assert not gui.get_property(current, "checked")
            assert current_storage_option(gui) == current
            gui.click(current)
            gui.wait_for_property(current, "checked", True)
            assert gui.get_property(current, "description") == description
            gui.click("onboardingStorageAmountButton")
            gui.wait_for_page("onboardingConnection")
            gui.click("onboardingConnectionButton")
            gui = harness.wait_for_main_window_reconnect()
            assert_node_shell_visible(gui)
            settings = read_settings_json(datadir)
            if settings_json:
                assert settings["prune"] == target, settings
            else:
                assert "prune" not in settings, settings
            assert "prune-prev" not in settings, settings
            with open(conf_path, encoding="utf8") as conf:
                assert conf.read() == original_config
        finally:
            harness.stop(cleanup=False)
            shutil.rmtree(tmpdir, ignore_errors=True)


def run_existing_profile_custom_storage_flow():
    for target, custom_gb in ((0, 7), (1907, 7), (4096, 7), (4096, 5), (6675, 7)):
        with tempfile.TemporaryDirectory(prefix="qml_custom_storage_") as tmpdir:
            datadir = os.path.join(tmpdir, "node0")
            write_datadir(datadir, pick_unused_port(), pick_unused_port(), extra_lines=[f"prune={target}"])
            harness = QmlTestHarness(
                datadir=datadir,
                reset_settings=False,
                start_onboarded=False,
                extra_args=["-regtest", "-disablewallet"],
            )
            try:
                harness.start()
                gui = harness.driver
                click_to_storage_amount(gui, datadir)
                if target == 0:
                    gui.click("storageReduceOption")
                gui.click("onboardingStorageSettingsButton")
                gui.wait_for_property("onboardingStorageSettingsPopup", "opened", True)
                gui.click("pruneTargetInput")
                gui.invoke("pruneTargetInput", "selectAll")
                gui.type_text("pruneTargetInput", str(custom_gb))
                gui.click("onboardingStorageSettingsCloseButton")
                gui.wait_for_property("onboardingStorageSettingsPopup", "visible", False)
                gui.click("storageCustomOption")
                gui.wait_for_property("storageCustomOption", "checked", True)
                assert not gui.get_property("storageReduceOption", "checked")
                description = gui.get_property("storageCustomOption", "description")
                assert description == f"Storing recent blocks up to {custom_gb} GB. Uses about {custom_gb} GB in total."

                for option in ("storageReduceOption", "storageReduceOption", "storageFullOption"):
                    gui.click(option)
                    gui.wait_for_property(option, "checked", True)
                    assert gui.get_property("storageCustomOption", "visible")
                    assert not gui.get_property("storageCustomOption", "checked")
                    assert gui.get_property("storageCustomOption", "description") == description
                gui.click("storageCustomOption")
                gui.wait_for_property("storageCustomOption", "checked", True)
                gui.click("onboardingStorageAmountButton")
                gui.wait_for_page("onboardingConnection")
                gui.click("onboardingConnectionButton")
                gui = harness.wait_for_main_window_reconnect()
                assert_node_shell_visible(gui)
                assert int(read_settings_json(datadir).get("prune", target)) == custom_gb * 1000000000 // (1024 * 1024)
            finally:
                harness.stop(cleanup=False)


def run_storage_mode_change_restart_flow():
    for original_target, settings_json in ((0, False), (4096, False), (0, True), (4096, True)):
        with tempfile.TemporaryDirectory(prefix="qml_storage_restart_") as tmpdir:
            datadir = os.path.join(tmpdir, "node0")
            rpc_port = pick_unused_port()
            config_target = (4096 if original_target == 0 else 0) if settings_json else original_target
            write_datadir(datadir, rpc_port, pick_unused_port(), extra_lines=[f"prune={config_target}"])
            if settings_json:
                write_settings_json(datadir, {"prune": original_target})
            conf_path = os.path.join(datadir, "bitcoin.conf")
            with open(conf_path, encoding="utf8") as conf:
                original_config = conf.read()
            node = subprocess.Popen([find_bitcoind(), f"-datadir={datadir}"],
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                wait_for_rpc(rpc_port)
                tip = rpc_call(rpc_port, "generatetodescriptor", [10, "raw(51)"])[-1]
            finally:
                stop_bitcoind(node, rpc_port)

            target = 2 * 1000000000 // (1024 * 1024) if original_target == 0 else 0
            selected = "storageReduceOption" if target else "storageFullOption"
            for restart in (False, True):
                harness = QmlTestHarness(
                    datadir=datadir,
                    reset_settings=False,
                    start_onboarded=False,
                    extra_args=["-regtest", "-disablewallet", "-qml_onboarded=0"],
                )
                try:
                    harness.start()
                    gui = harness.driver
                    click_to_storage_amount(gui, datadir)
                    if restart:
                        assert current_storage_option(gui) == selected
                        assert gui.get_property(selected, "checked")
                    else:
                        original = current_storage_option(gui)
                        assert original != selected
                        gui.click(selected)
                        gui.wait_for_property(selected, "checked", True)
                        assert current_storage_option(gui) == original
                    gui.click("onboardingStorageAmountButton")
                    gui.wait_for_page("onboardingConnection")
                    gui.click("onboardingConnectionButton")
                    gui = harness.wait_for_main_window_reconnect()
                    assert_node_shell_visible(gui)
                    wait_for_rpc(rpc_port)
                    info = rpc_call(rpc_port, "getblockchaininfo")
                    assert info["pruned"] == (target > 0), info
                    if target:
                        assert info["prune_target_size"] == target * 1024 * 1024, info
                    assert info["bestblockhash"] == tip, info
                    assert int(read_settings_json(datadir).get("prune", config_target)) == target
                    with open(conf_path, encoding="utf8") as conf:
                        assert conf.read() == original_config
                finally:
                    harness.stop(cleanup=False)


def run_pruned_storage_rebuild_flow():
    with tempfile.TemporaryDirectory(prefix="qml_pruned_storage_") as tmpdir:
        datadir = os.path.join(tmpdir, "node0")
        rpc_port = pick_unused_port()
        write_datadir(datadir, rpc_port, pick_unused_port(), extra_lines=["prune=4096"])
        node = subprocess.Popen([find_bitcoind(), f"-datadir={datadir}", "-fastprune"],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            wait_for_rpc(rpc_port)
            rpc_call(rpc_port, "generatetodescriptor", [700, "raw(51)"])
            rpc_call(rpc_port, "pruneblockchain", [400])
            assert rpc_call(rpc_port, "getblockchaininfo")["pruneheight"] > 1
        finally:
            stop_bitcoind(node, rpc_port)

        harness = QmlTestHarness(
            datadir=datadir,
            reset_settings=False,
            start_onboarded=False,
            extra_args=["-regtest", "-disablewallet"],
        )
        try:
            harness.start()
            gui = harness.driver
            click_to_storage_amount(gui, datadir)
            assert current_storage_option(gui) == "storageReduceOption"
            assert "Requires downloading the full blockchain again." in gui.get_property("storageFullOption", "errorText")
            gui.click("storageFullOption")
            gui.wait_for_property("storageFullOption", "checked", True)
            gui.click("onboardingStorageAmountButton")
            gui.wait_for_page("onboardingConnection")
            gui.click("onboardingConnectionButton")
            gui = harness.wait_for_main_window_reconnect()
            gui.wait_for_property("nodeRuntimeDialog", "opened", True)
            message = gui.get_property("nodeRuntimeDialogMessage", "text")
            assert "redownload the entire blockchain" in message, message
            assert "Do you want to rebuild the databases now?" in message, message
            gui.click("nodeRuntimeDialogButtonOk")
            gui.wait_for_property("nodeRuntimeDialog", "opened", False)
            assert_node_shell_visible(gui)
            wait_for_rpc(rpc_port)
            info = rpc_call(rpc_port, "getblockchaininfo")
            assert not info["pruned"], info
            assert info["blocks"] == 0, info
            assert int(read_settings_json(datadir)["prune"]) == 0
        finally:
            harness.stop(cleanup=False)


def run_reset_storage_badge_flow():
    for config_target, settings_target in ((4096, None), (None, 4096), (4096, 1907), (0, 4096)):
        with tempfile.TemporaryDirectory(prefix="qml_reset_storage_") as tmpdir:
            datadir = os.path.join(tmpdir, "node0")
            rpc_port = pick_unused_port()
            write_datadir(datadir, rpc_port, pick_unused_port(), extra_lines=(
                [] if config_target is None else [f"prune={config_target}"]
            ))
            if settings_target is not None:
                write_settings_json(datadir, {"prune": settings_target, "qml_onboarded": True})
            conf_path = os.path.join(datadir, "bitcoin.conf")
            with open(conf_path, encoding="utf8") as conf:
                original_config = conf.read()
            harness = QmlTestHarness(
                datadir=datadir,
                reset_settings=True,
                start_onboarded=False,
                extra_args=["-regtest", "-disablewallet"],
            )
            try:
                harness.start()
                gui = harness.driver
                click_to_storage_amount(gui, datadir)
                current = "storageReduceOption" if config_target else "storageFullOption"
                assert current_storage_option(gui) == current
                assert gui.get_property(current, "checked")
                if config_target:
                    assert gui.get_property(current, "description") == "Uses about 5GB. For regular wallet use."
                gui.click("onboardingStorageAmountButton")
                gui.wait_for_page("onboardingConnection")
                gui.click("onboardingConnectionButton")
                gui = harness.wait_for_main_window_reconnect()
                assert_node_shell_visible(gui)
                wait_for_rpc(rpc_port)
                info = rpc_call(rpc_port, "getblockchaininfo")
                assert info["pruned"] == bool(config_target), info
                if config_target:
                    assert info["prune_target_size"] == config_target * 1024 * 1024, info
                assert "prune" not in read_settings_json(datadir)
                with open(conf_path, encoding="utf8") as conf:
                    assert conf.read() == original_config
            finally:
                harness.stop(cleanup=False)


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
    run_existing_profile_storage_choices_flow()
    run_existing_profile_custom_storage_flow()
    run_storage_mode_change_restart_flow()
    run_pruned_storage_rebuild_flow()
    run_reset_storage_badge_flow()
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
