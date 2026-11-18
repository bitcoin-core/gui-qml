#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Exercise the real pre-init onboarding flow.

This test intentionally launches bitcoin-core-app without -datadir so
RunPreInitOnboarding() is observable through the pre-init test bridge.
"""

import json
import os
import subprocess
import time
from qml_test_harness import cleanup_test_directory, create_test_directory
from qml_scenarios import (
    run_scenarios,
    scenario,
)
from qml_test_harness import (
    QmlTestHarness,
    assert_node_shell_visible,
    assert_onboarding_wallet_creation_visible,
    assert_wallet_shell_visible,
    click_onboarding_button,
    complete_preinit_onboarding,
    dump_qml_tree,
    setup_datadir,
)
from qml_process_checks import check_gui_exit
from qml_qt_release_lib import (
    compat_bitcoin_qt_or_skip,
    launch_bitcoin_qt,
    process_output,
    stop_bitcoin_qt,
    wait_for_rpc_or_process_exit,
)
from qml_wallet_test_lib import (
    RPC_PASS,
    RPC_USER,
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
    # Keep the pre-init engine wiring check here; detailed link confirmation
    # and reuse assertions live in the stage-two settings integration module.
    gui.click("aboutWebsiteRow")
    gui.wait_for_property("aboutExternalLinkPopup", "opened", True, timeout_ms=5000)
    gui.click("externalLinkCancel")
    gui.wait_for_property("aboutExternalLinkPopup", "visible", False, timeout_ms=5000)
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

    click_onboarding_button(gui, "onboardingCoverButton")
    gui.wait_for_page("onboardingStrengthen", timeout_ms=5000)
    click_onboarding_button(gui, "onboardingStrengthenButton")
    gui.wait_for_page("onboardingBlockchain", timeout_ms=5000)
    click_onboarding_button(gui, "onboardingBlockchainButton")
    gui.wait_for_page("onboardingBlockclock", timeout_ms=5000)
    click_onboarding_button(gui, "onboardingBlockclockButton")
    gui.wait_for_page("onboardingStorageLocation", timeout_ms=5000)
    gui.wait_for_property("storageCustomLocationOption", "checked", True, timeout_ms=5000)
    assert gui.get_property("storageCustomLocationOption", "customDir") == datadir

    click_onboarding_button(gui, "onboardingStorageLocationButton")
    gui.wait_for_page("onboardingStorageAmount", timeout_ms=5000)
    if expect_custom_storage:
        gui.wait_for_property("storageCustomOption", "checked", True, timeout_ms=5000)
    else:
        assert (
            gui.get_property("storageReduceOption", "checked")
            or gui.get_property("storageFullOption", "checked")
        ), "Expected an existing-profile storage amount selection"

    click_onboarding_button(gui, "onboardingStorageAmountButton")
    gui.wait_for_page("onboardingConnection", timeout_ms=5000)


def finish_existing_profile_preinit_and_reconnect(harness, datadir, expect_custom_storage=True):
    gui = harness.driver
    click_existing_profile_to_connection(gui, datadir, expect_custom_storage)
    click_onboarding_button(gui, "onboardingConnectionButton")
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


# Bitcoin-Qt stores the display unit as its own serialized BitcoinUnit type.
BITCOIN_QT_BTC_UNIT = "DisplayBitcoinUnit=@Variant(\\0\\0\\0\\x7f\\0\\0\\0\\fBitcoinUnit\\0\\0)"
BITCOIN_QT_SAT_UNIT = "DisplayBitcoinUnit=@Variant(\\0\\0\\0\\x7f\\0\\0\\0\\fBitcoinUnit\\0\\x3)"


def write_bitcoin_qt_settings(bitcoin_qt, tmpdir, home, chain_args, conf_lines):
    """Run a released Bitcoin-Qt once and return the settings file it wrote."""
    rpc_port = pick_unused_port()
    datadir = os.path.join(tmpdir, "bitcoin_qt_" + (chain_args[0].lstrip("-") if chain_args else "main"))
    config_home = os.path.join(tmpdir, "bitcoin_qt_config")
    os.makedirs(datadir, exist_ok=True)
    with open(os.path.join(datadir, "bitcoin.conf"), "w", encoding="utf8") as conf:
        conf.write("\n".join(conf_lines + [f"rpcport={rpc_port}"]) + "\n")
    process = launch_bitcoin_qt(bitcoin_qt, datadir, datadir, config_home=config_home, home=home, extra_args=chain_args)
    try:
        wait_for_rpc_or_process_exit(process, rpc_port, datadir)
    finally:
        stop_bitcoin_qt(process, rpc_port)
    assert process.returncode == 0, process_output(process, datadir)
    name = "Bitcoin-Qt-regtest" if chain_args == ["-regtest"] else "Bitcoin-Qt"
    path = os.path.join(config_home, "Bitcoin", f"{name}.conf")
    assert os.path.isfile(path), path
    return path


def run_bitcoin_qt_settings_adoption_flow():
    """A Bitcoin-Qt user's datadir and display unit carry over without loss."""
    bitcoin_qt = compat_bitcoin_qt_or_skip()
    if not bitcoin_qt:
        return 77
    harness = QmlTestHarness(
        use_datadir_arg=False,
        reset_settings=False,
        start_onboarded=False,
        extra_args=["-regtest", "-disablewallet"],
    )
    tmpdir = harness.tmpdir
    gui = None
    try:
        common = ["server=1", "listen=0", "connect=0", "dnsseed=0", "fixedseeds=0", "listenonion=0",
                  "rpcbind=127.0.0.1", "rpcallowip=127.0.0.1", f"rpcuser={RPC_USER}", f"rpcpassword={RPC_PASS}"]
        # The default-chain store holds the data directory; the regtest store the unit.
        # Pruning avoids the default chain's modal disk-space warning.
        main_store = write_bitcoin_qt_settings(bitcoin_qt, tmpdir, harness.home_dir, [], common + ["prune=550"])
        regtest_store = write_bitcoin_qt_settings(bitcoin_qt, tmpdir, harness.home_dir, ["-regtest"],
                                                  ["[regtest]"] + common)

        # Without user input Bitcoin-Qt saves only its defaults. Change the
        # saved values in its own format, so adoption differs from our defaults.
        legacy_datadir = os.path.join(tmpdir, "bitcoin_qt_profile")
        write_datadir(legacy_datadir, pick_unused_port(), pick_unused_port())
        with open(main_store, encoding="utf8") as store:
            main_settings = store.read()
        assert "strDataDir=" in main_settings and BITCOIN_QT_BTC_UNIT in main_settings, main_settings
        main_settings = "\n".join(f"strDataDir={legacy_datadir}" if line.startswith("strDataDir=") else line
                                  for line in main_settings.splitlines()) + "\n"
        with open(regtest_store, encoding="utf8") as store:
            regtest_settings = store.read()
        assert BITCOIN_QT_BTC_UNIT in regtest_settings, regtest_settings
        regtest_settings = regtest_settings.replace(BITCOIN_QT_BTC_UNIT, BITCOIN_QT_SAT_UNIT)

        # -test-settings-dir selects INI files there; the syntax is Bitcoin-Qt's native Linux format.
        target = os.path.join(harness.config_home, "Bitcoin")
        os.makedirs(target, exist_ok=True)
        for name, text in (("Bitcoin-Qt.ini", main_settings), ("Bitcoin-Qt-regtest.ini", regtest_settings)):
            with open(os.path.join(target, name), "w", encoding="utf8") as store:
                store.write(text)

        harness.start()
        gui = finish_existing_profile_preinit_and_reconnect(harness, legacy_datadir, expect_custom_storage=False)
        assert_node_shell_visible(gui)
        gui.click("nodeSettingsButton")
        gui.wait_for_page("settingsView")
        gui.wait_for_property("settingsView", "optionsEditable", True)
        gui.click("settingsSidebar_display")
        gui.wait_for_property("settingsView", "selectedSectionId", "display")
        gui.wait_for_property("displayUnitPicker", "currentValue", 3)

        process = harness.process
        gui.request_quit()
        process.wait(timeout=30)
        _, stderr = process.communicate(timeout=5)
        # An unregistered BitcoinUnit loses the stored value with a Qt warning.
        check_gui_exit(process.returncode, stderr.decode("utf8", errors="replace"), socket_path=harness.socket_path)
    except Exception:
        if gui is not None:
            dump_qml_tree(gui)
        raise
    finally:
        harness.stop()


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
            cleanup_test_directory(tmpdir)


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
    tmpdir = create_test_directory(prefix="qml704_", dir="/tmp")
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
        cleanup_test_directory(tmpdir)


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
        cleanup_test_directory(tmpdir)


def run_existing_profile_preserves_root_wallet_discovery_flow():
    tmpdir = create_test_directory(prefix="qml704_wallets_", dir="/tmp")
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
        cleanup_test_directory(tmpdir)


def run_existing_profile_detects_explicit_walletdir_flow():
    tmpdir = create_test_directory(prefix="qml704_walletdir_", dir="/tmp")
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
        cleanup_test_directory(tmpdir)


def run_qml_onboarded_override_reloads_saved_settings_flow():
    tmpdir = create_test_directory(prefix="qml704_override_", dir="/tmp")
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

        click_onboarding_button(gui, "onboardingConnectionButton")
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
        cleanup_test_directory(tmpdir)


def run_native_startup_failure_reopens_saved_setup_flow():
    """The actual Core DNS failure must not make partial setup permanent."""
    from qml_process_checks import check_gui_exit, unexpected_gui_stderr
    tmpdir = create_test_directory(prefix="qml_setup_recovery_", dir="/tmp")
    datadir = setup_datadir(tmpdir)
    write_settings_json(datadir, {"qml_onboarded": True, "proxy": "no-such-host.invalid:9050", "prune": 550})
    failed = QmlTestHarness(datadir=datadir, tmpdir=tmpdir, reset_settings=False,
                           start_onboarded=False, extra_args=["-regtest", "-disablewallet", "-lang=en"])
    recovered = None
    try:
        failed.start()
        failed.driver.wait_for_property("nodeRuntimeDialog", "opened", True, timeout_ms=30000)
        assert "Invalid -proxy address or hostname" in failed.driver.get_property("nodeRuntimeDialogMessage", "text")
        try:
            failed.driver.click("nodeRuntimeDialogButtonOk")
        except ConnectionError:
            # Failure shutdown may close the bridge before the click reply.
            pass
        failed.process.wait(timeout=20)
        settings = read_settings_json(datadir)
        assert settings.get("qml_onboarded") is False, settings
        assert settings.get("proxy") == "no-such-host.invalid:9050", settings
        failed.stop(cleanup=False)
        stdout, stderr = failed.process.communicate()
        print("Original failure stdout:\n" + stdout.decode("utf8", errors="replace"), flush=True)
        stderr = stderr.decode("utf8", errors="replace")
        print("Original failure stderr:\n" + stderr, flush=True)
        assert failed.process.returncode == 1, (failed.process.returncode, stderr)
        expected = "Invalid -proxy address or hostname: 'no-such-host.invalid:9050'"
        unexpected = unexpected_gui_stderr(stderr, socket_path=failed.socket_path)
        assert all(line in (expected, "Error: " + expected) for line in unexpected), unexpected
        print("Retained native startup failure: exit=1; marker=false; proxy intent unchanged", flush=True)

        recovered = QmlTestHarness(datadir=datadir, tmpdir=tmpdir, reset_settings=False,
                                  start_onboarded=False, extra_args=["-regtest", "-disablewallet", "-lang=en"])
        recovered.start()
        gui = recovered.driver
        click_existing_profile_to_connection(gui, datadir)
        gui.click("connectionSettingsButton")
        gui.wait_for_page("connectionSettingsPage")
        gui.click("proxySettingsRow")
        gui.wait_for_page("proxySettingsPage")
        gui.wait_for_property("proxyEnableSwitch", "checked", True)
        gui.click("proxyEnableSwitch")
        gui.wait_for_property("proxyEnableSwitch", "checked", False)
        gui.wait_for_property("proxySettingsPage", "proxyDraft.pending", False)
        gui.wait_for_property("proxySettingsSaveButton", "enabled", True)
        gui.click("proxySettingsSaveButton")
        gui.wait_for_page("connectionSettingsPage")
        # A visible underlying page during the stack transition does not prove
        # the asynchronous draft was applied. Observe its authoritative model.
        gui.wait_for_property("connectionSettingsPage", "settingsModel.proxyEnabled", False)
        gui.wait_for_property("connectionSettingsPage", "coreSettingsModel.pending", False)
        gui.click("onboardingConnectionSettingsCloseButton")
        gui.wait_for_property("onboardingConnectionSettingsPopup", "visible", False)
        click_onboarding_button(gui, "onboardingConnectionButton")
        gui = recovered.wait_for_main_window_reconnect()
        assert_node_shell_visible(gui)
        settings = read_settings_json(datadir)
        assert settings.get("qml_onboarded") is True, settings
        assert settings.get("proxy") is False, settings
        assert settings.get("proxy-prev") == "no-such-host.invalid:9050", settings
        assert settings.get("prune") == 550, settings
        recovered.stop(cleanup=False)
        _, stderr = recovered.process.communicate()
        check_gui_exit(recovered.process.returncode, stderr.decode("utf8", errors="replace"), socket_path=recovered.socket_path)
        print("Recovered original saved setup: actual Core ready; marker=true; exact550MiB retained; exit=0", flush=True)
    except Exception:
        print("Retained actual setup settings: " + repr(read_settings_json(datadir)), flush=True)
        for label, harness in (("original", failed), ("recovery", recovered)):
            if harness is None:
                continue
            if harness.driver is not None:
                dump_qml_tree(harness.driver)
            harness.stop(cleanup=False)
            stdout, stderr = harness.process.communicate()
            print(f"{label} process diagnostic exit={harness.process.returncode}\n" + stdout.decode("utf8", errors="replace") + stderr.decode("utf8", errors="replace"), flush=True)
        raise
    finally:
        failed.stop(cleanup=False)
        if recovered is not None:
            recovered.stop(cleanup=False)
        cleanup_test_directory(tmpdir)



def run_reset_retains_original_backup_through_finish():
    """A single reset handoff must retain the actual pre-reset Core settings."""
    from qml_process_checks import check_gui_exit
    tmpdir = create_test_directory(prefix="qml_reset_receipt_", dir="/tmp")
    datadir = setup_datadir(tmpdir)
    original = {"qml_onboarded": True, "prune": 953, "dbcache": 512, "wallet": []}
    write_settings_json(datadir, original)
    harness = QmlTestHarness(datadir=datadir, tmpdir=tmpdir, reset_settings=True,
                             start_onboarded=False, extra_args=["-regtest", "-disablewallet", "-lang=en"])
    try:
        harness.start()
        click_existing_profile_to_connection(harness.driver, datadir, expect_custom_storage=False)
        click_onboarding_button(harness.driver, "onboardingConnectionButton")
        gui = harness.wait_for_main_window_reconnect()
        assert_node_shell_visible(gui)
        with open(os.path.join(datadir, "regtest", "settings.json.bak"), encoding="utf8") as backup:
            actual = {key: value for key, value in json.load(backup).items() if key != "_warning_"}
        assert actual == original, (actual, original)
        assert read_settings_json(datadir).get("qml_onboarded") is True
        harness.stop(cleanup=False)
        _, stderr = harness.process.communicate()
        check_gui_exit(harness.process.returncode, stderr.decode("utf8", errors="replace"), socket_path=harness.socket_path)
        print("Reset receipt: original backup retained; marker=true; process exit=0; stderr inspected", flush=True)
    finally:
        harness.stop(cleanup=False)
        cleanup_test_directory(tmpdir)

if __name__ == "__main__":
    raise SystemExit(run_scenarios('Pre-init onboarding engine and persisted configuration boundaries', [
        scenario("wallet-onboarding", 4, run_wallet_enabled_flow),
        scenario("node-onboarding", 2, run_wallet_disabled_flow),
        scenario("reset-retains-original-backup", 2, run_reset_retains_original_backup_through_finish),
        scenario("native-startup-failure-recovery", 2, run_native_startup_failure_reopens_saved_setup_flow),
        scenario("existing-profile-onboarding", 2, run_existing_profile_full_onboarding_flow),
        scenario("configured-datadir-source", 2, run_configured_datadir_preserves_config_source_flow),
        scenario("root-wallet-discovery", 4, run_existing_profile_preserves_root_wallet_discovery_flow),
        scenario("explicit-walletdir-discovery", 4, run_existing_profile_detects_explicit_walletdir_flow),
        scenario("onboarded-override-settings", 2, run_qml_onboarded_override_reloads_saved_settings_flow),
        scenario("bitcoin-qt-settings-adoption", 2, run_bitcoin_qt_settings_adoption_flow),
    ]))
