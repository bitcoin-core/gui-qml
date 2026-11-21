#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Bridge, onboarding, configuration sources and GUI reset process boundaries."""

import json
import os
import subprocess
import tempfile
import threading
import time
from pathlib import Path
from qml_driver import QmlDriverError
from qml_process_checks import check_gui_exit, unexpected_gui_stderr
from qml_smoke_lib import wait_until
from qml_wallet_test_lib import RPC_PASS, RPC_USER, rpc_call, wait_for_rpc
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
    qml_qpa_platform,
    qsettings_sandbox_args,
    walk_onboarding_to_connection,
)


DESKTOP_PAYMENT_ADDRESS = "bcrt1qdavt4j2sd7dlhqsavtnfxvzppw6k7qy97tmnu9"


def unicode_startup_profile():
    """The real process argv and Core profile retain non-ASCII path identity."""
    with tempfile.TemporaryDirectory(prefix="u8-") as temporary:
        profile = Path(temporary) / "caf\u00e9-\u94b1\u5305-\U0001f600"
        profile.mkdir()
        with gui_process(tmpdir=str(profile), no_listen_arg=False,
                         extra_args=["-nolisten", f"-rpcuser={RPC_USER}", f"-rpcpassword={RPC_PASS}"]) as harness:
            wait_for_rpc(harness.rpc_port)
            actual = Path(rpc_call(harness.rpc_port, "getrpcinfo")["logpath"]).resolve()
            assert actual.is_relative_to(Path(harness.datadir).resolve()), actual
            assert profile.name in actual.parts, actual
            assert actual.is_file(), actual
            assert rpc_call(harness.rpc_port, "getblockchaininfo")["chain"] == "regtest"


def desktop_payment(label, amount="0.01000000"):
    return f"bitcoin:{DESKTOP_PAYMENT_ADDRESS}?amount={amount}&label={label}&message=explicit-inspection-only"


def desktop_mailbox(gui, count):
    gui.wait_for_object("desktopRequestsBar", timeout_ms=30000)
    gui.wait_for_property("desktopRequestsBar", "mailbox.pendingCount", count, timeout_ms=10000)
    assert not gui.get_property("desktopRequestsBar", "mailbox.hasInspection"), "Ingress must not create a Source inspection"
    assert not gui.get_property("desktopRequestsBar", "mailbox.canContinue"), "Ingress must not authorize a wallet action"


def desktop_transfer(stdout, status, count, allow_inspected=False):
    """Inspect the gated Native startup observation, never inventing a receipt."""
    prefix = "R40_TRANSFER_JSON "
    lines = [line[len(prefix):] for line in stdout.decode("utf8", errors="strict").splitlines() if line.startswith(prefix)]
    assert len(lines) == 1, lines
    record = json.loads(lines[0])
    assert record["version"] == 1 and record["status"] == status, record
    keys, receipts = record["producer_keys"], record["receipts"]
    assert len(keys) == len(receipts) == count and len(set(keys)) == count, record
    assert all(isinstance(key, str) and 0 < len(key) <= 128 for key in keys), record
    for index, receipt in enumerate(receipts):
        for name in ("delivery", "inspection"):
            value = receipt[name]
            assert isinstance(value, str) and 0 < len(value) <= 20 and value.isdecimal(), record
            assert str(int(value)) == value and 0 <= int(value) < 2**64, record
        assert keys[index].endswith(f":{index}"), record
        assert int(receipt["delivery"]) == index + 1, record
        assert type(receipt["disposition"]) is int, record
        if allow_inspected and receipt["disposition"] == 1:  # Version1 Disposition::Accepted.
            assert int(receipt["inspection"]) > 0, record
        else:
            assert receipt["disposition"] == 0 and receipt["inspection"] == "0", record  # Version1 Disposition::Queued.
    return record


def desktop_intent_primary_arguments():
    """Ready inspects one retained input at a time without choosing a wallet target."""
    harness = QmlTestHarness(no_listen_arg=False, extra_args=[f"-rpcuser={RPC_USER}", f"-rpcpassword={RPC_PASS}", "-nolisten"])
    source = Path(harness.tmpdir) / "request from file.uri"
    source.write_text(desktop_payment("file-source", "0.02000000"), encoding="utf8")
    harness.extra_args.extend([desktop_payment("uri-source"), str(source)])
    try:
        harness.start()
        gui = harness.driver
        gui.wait_for_object("desktopRequestsBar", timeout_ms=30000)
        wait_for_rpc(harness.rpc_port)
        assert rpc_call(harness.rpc_port, "listwallets") == []
        assert rpc_call(harness.rpc_port, "getrawmempool") == []
        # Ready admits only one standalone inspection. The next original input
        # waits until that presentation retires; neither selects a wallet target.
        original_sources = set()
        for remaining, label, amount in [(1, "uri-source", 1000000), (0, "file-source", 2000000)]:
            gui.wait_for_property("desktopRequestsBar", "mailbox.hasInspection", True, timeout_ms=10000)
            gui.wait_for_property("desktopRequestsBar", "mailbox.inspection.publicLabel", label, timeout_ms=10000)
            gui.wait_for_property("desktopRequestsBar", "mailbox.inspectionPending", False, timeout_ms=10000)
            gui.wait_for_property("desktopRequestsBar", "mailbox.pendingCount", remaining, timeout_ms=10000)
            assert gui.get_property("desktopRequestsBar", "mailbox.inspection.error") == ""
            assert gui.get_property("desktopRequestsBar", "mailbox.inspection.address") == DESKTOP_PAYMENT_ADDRESS
            assert gui.get_property("desktopRequestsBar", "mailbox.inspection.publicLabel") == label
            assert gui.get_property("desktopRequestsBar", "mailbox.inspection.amount") == amount
            assert gui.get_enum_property("desktopRequestsBar", "mailbox.inspection.state")["key"] == "Inspecting"
            assert gui.get_enum_property("desktopRequestsBar", "mailbox.inspection.sourceKind")["key"] == "PaymentSource"
            assert gui.get_enum_property("desktopRequestsBar", "mailbox.inspection.outcome")["key"] == "NoOutcome"
            original = gui.get_property("desktopRequestsBar", "mailbox.inspection.requestId")
            assert original > 0 and original not in original_sources
            original_sources.add(original)
            assert not gui.get_property("desktopRequestsBar", "mailbox.inspection.canApply")
            assert not gui.get_property("desktopRequestsBar", "mailbox.inspection.canReview")
            assert not gui.get_property("desktopRequestContinue", "enabled")
            gui.click("desktopRequestCloseInspection")
            if remaining == 0:
                gui.wait_for_property("desktopRequestsBar", "mailbox.hasInspection", False, timeout_ms=10000)
        assert rpc_call(harness.rpc_port, "listwallets") == []
        assert rpc_call(harness.rpc_port, "getrawmempool") == []
        gui.request_quit()
        stdout, stderr = harness.process.communicate(timeout=30)
        check_gui_exit(harness.process.returncode, stderr.decode("utf8", errors="replace"), socket_path=harness.socket_path)
        desktop_transfer(stdout, "primary", 2)
    finally:
        harness.stop()


def desktop_intent_profile_isolation():
    """Separate profile listeners and original queues can run concurrently."""
    with gui_process(no_listen_arg=False, extra_args=["-disablewallet", "-nolisten", desktop_payment("first-profile")]) as first:
        desktop_mailbox(first.driver, 1)
        with gui_process(no_listen_arg=False, extra_args=["-disablewallet", "-nolisten", desktop_payment("second-profile-a"), desktop_payment("second-profile-b")]) as second:
            desktop_mailbox(second.driver, 2)
            assert first.process.poll() is None
            assert first.driver.get_property("desktopRequestsBar", "mailbox.pendingCount") == 1
            second.driver.click("desktopRequestDismiss")
            second.driver.wait_for_property("desktopRequestsBar", "mailbox.pendingCount", 1, timeout_ms=10000)
            assert first.driver.get_property("desktopRequestsBar", "mailbox.pendingCount") == 1
        assert first.process.poll() is None


def desktop_intent_secondary_forwarding(wallet_enabled=False):
    """A second real GUI forwards once and exits before local settings/Core effects."""
    wallet_args = [] if wallet_enabled else ["-disablewallet"]
    with gui_process(extra_args=[*wallet_args, f"-rpcuser={RPC_USER}", f"-rpcpassword={RPC_PASS}"]) as primary:
        gui = primary.driver
        wait_for_rpc(primary.rpc_port)
        if not wallet_enabled:
            assert_node_shell_visible(gui)
        else:
            wait_until(lambda: gui.get_enum_property("desktopRequestsBar", "mailbox.state")["key"] == "Ready", "original desktop receiver readiness")
        shared_settings = Path(primary.datadir) / "regtest" / "settings.json"
        assert shared_settings.is_file(), "The primary must finish its real settings commit before this barrier"
        original_settings = shared_settings.read_bytes()
        original_config = (Path(primary.datadir) / "bitcoin.conf").read_bytes()
        with tempfile.TemporaryDirectory(prefix="qml_forward_") as temporary:
            directory = Path(temporary)
            config = directory / "config"
            organization = config / "BitcoinCore"
            organization.mkdir(parents=True)
            for name in ("BitcoinCore-App", "BitcoinCore-App-regtest"):
                (organization / f"{name}.conf").write_text("[General]\nr40_preserved=original-value\n", encoding="utf8")
            before = {str(path.relative_to(config)): path.read_bytes() for path in config.rglob("*") if path.is_file()}
            home = directory / "home"
            home.mkdir()
            source = directory / "forwarded file.uri"
            source.write_text(desktop_payment("secondary-file"), encoding="utf8")
            bridge = directory / "secondary.sock"
            local_effects = [directory / "secondary-settings.json", directory / "secondary.pid", directory / "secondary-core.log"]
            environment = dict(os.environ)
            environment["QT_QPA_PLATFORM"] = qml_qpa_platform()
            environment["HOME"] = str(home)
            settings_args = qsettings_sandbox_args(environment, str(config))
            command = [primary.gui_binary, f"-datadir={primary.datadir}", "-regtest", "-disablewallet", "-nolisten", "-qml_onboarded=1",
                       f"-test-automation={bridge}", *settings_args, "-resetguisettings", f"-settings={local_effects[0]}",
                       f"-pid={local_effects[1]}", f"-debuglogfile={local_effects[2]}", desktop_payment("secondary-uri"), source.as_uri()]
            print("Starting secondary GUI: " + " ".join(command), flush=True)
            secondary = subprocess.Popen(command, env=environment, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                stdout, stderr = secondary.communicate(timeout=30)
            except subprocess.TimeoutExpired as expired:
                print(f"R40_SECONDARY_WATCHDOG pid={secondary.pid} timeout=30 exit={secondary.poll()}", flush=True)
                print("Secondary watchdog stdout:\n" + (expired.output or b"").decode("utf8", errors="replace"), flush=True)
                print("Secondary watchdog stderr:\n" + (expired.stderr or b"").decode("utf8", errors="replace"), flush=True)
                print("Secondary watchdog paths: " + json.dumps({str(path): path.exists() for path in [bridge, *local_effects]}), flush=True)
                raise
            finally:
                if secondary.poll() is None:
                    secondary.terminate()
                    try:
                        cleanup_stdout, cleanup_stderr = secondary.communicate(timeout=10)
                    except subprocess.TimeoutExpired:
                        secondary.kill()
                        cleanup_stdout, cleanup_stderr = secondary.communicate(timeout=10)
                    print(f"R40_SECONDARY_CLEANUP pid={secondary.pid} exit={secondary.returncode}", flush=True)
                    print("Secondary cleanup stdout:\n" + cleanup_stdout.decode("utf8", errors="replace"), flush=True)
                    print("Secondary cleanup stderr:\n" + cleanup_stderr.decode("utf8", errors="replace"), flush=True)
            text = stdout.decode("utf8", errors="replace")
            print("Secondary stdout:\n" + text, flush=True)
            print("Secondary stderr:\n" + stderr.decode("utf8", errors="replace"), flush=True)
            check_gui_exit(secondary.returncode, stderr.decode("utf8", errors="replace"), socket_path=str(bridge))
            desktop_transfer(stdout, "forwarded", 2, allow_inspected=wallet_enabled)
            if wallet_enabled:
                gui.wait_for_object("desktopRequestsBar", timeout_ms=30000)
                gui.wait_for_property("desktopRequestsBar", "mailbox.inspection.publicLabel", "secondary-uri", timeout_ms=10000)
                gui.wait_for_property("desktopRequestsBar", "mailbox.inspectionPending", False, timeout_ms=10000)
                gui.wait_for_property("desktopRequestsBar", "mailbox.pendingCount", 1, timeout_ms=10000)
                assert gui.get_enum_property("desktopRequestsBar", "mailbox.inspection.outcome")["key"] == "NoOutcome"
                assert not gui.get_property("desktopRequestsBar", "mailbox.inspection.canApply")
                assert not gui.get_property("desktopRequestsBar", "mailbox.inspection.canReview")
                assert not gui.get_property("desktopRequestContinue", "enabled")
                assert rpc_call(primary.rpc_port, "listwallets") == []
                assert rpc_call(primary.rpc_port, "getrawmempool") == []
            else:
                desktop_mailbox(gui, 2)
            assert not bridge.exists(), "Secondary role discovery must precede bridge/window startup"
            assert not any(path.exists() for path in local_effects), [(str(path), path.exists()) for path in local_effects]
            after = {str(path.relative_to(config)): path.read_bytes() for path in config.rglob("*") if path.is_file()}
            assert after == before, "Forwarding must precede both default and chain QSettings reset/write"
            assert shared_settings.read_bytes() == original_settings, "Secondary must not rewrite the primary's Core settings"
            assert (Path(primary.datadir) / "bitcoin.conf").read_bytes() == original_config
            assert rpc_call(primary.rpc_port, "getblockchaininfo")["chain"] == "regtest"
            assert primary.process.poll() is None


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


def runtime_block_write_failure_exit_status():
    """A real Core AbortNode failure must survive normal GUI teardown."""
    harness = QmlTestHarness(extra_args=["-disablewallet", f"-rpcuser={RPC_USER}", f"-rpcpassword={RPC_PASS}"])
    modes = {}
    result = {}
    worker = None

    def restore_modes():
        for path, mode in modes.items():
            if path.exists():
                path.chmod(mode)

    def generate():
        try:
            result["value"] = rpc_call(harness.rpc_port, "generatetodescriptor", [1, "raw(51)"])
        except Exception as error:
            result["error"] = str(error)

    try:
        harness.start()
        gui = harness.driver
        assert_node_shell_visible(gui)
        wait_for_rpc(harness.rpc_port)
        assert rpc_call(harness.rpc_port, "getblockchaininfo")["chain"] == "regtest"
        blocks = Path(harness.datadir) / "regtest" / "blocks"
        files = sorted(blocks.glob("blk*.dat"))
        assert files, "Initialized Core must own a real block file before the permission barrier"
        for path in [blocks, *files]:
            modes[path] = path.stat().st_mode & 0o777
            path.chmod(0o555 if path.is_dir() else 0o444)
        try:
            with files[0].open("r+b"):
                pass
        except PermissionError:
            pass
        else:
            raise AssertionError("Environment did not enforce the disposable block-file permission barrier")
        worker = threading.Thread(target=generate, name="runtime-fatal-rpc")
        worker.start()
        gui.wait_for_property("nodeRuntimeDialog", "opened", True, timeout_ms=30000)
        message = gui.get_property("nodeRuntimeDialogMessage", "text")
        assert "Failed to write block." in message, message
        # Core has already failed. Restore the disposable files before release,
        # so ordinary teardown cannot add a second unrelated storage failure.
        restore_modes()
        try:
            gui.click("nodeRuntimeDialogButtonOk")
        except ConnectionError:
            pass  # Core may close the bridge before the reply reaches us.
        harness.process.wait(timeout=30)
        worker.join(timeout=10)
        assert not worker.is_alive(), "The original public RPC continuation did not finish"
        stdout, stderr = harness.process.communicate()
        text = stderr.decode("utf8", errors="replace")
        print("Runtime storage-failure stdout:\n" + stdout.decode("utf8", errors="replace"), flush=True)
        print("Runtime storage-failure stderr:\n" + text, flush=True)
        print(f"R36_RUNTIME_FATAL app_exit={harness.process.returncode} rpc={result!r}", flush=True)
        assert harness.process.returncode == 1, (harness.process.returncode, text)
        unexpected = unexpected_gui_stderr(text, socket_path=harness.socket_path)
        assert unexpected and all("Failed to write block." in line for line in unexpected), unexpected
    finally:
        restore_modes()
        harness.stop()
        if worker is not None:
            worker.join(timeout=10)
            assert not worker.is_alive(), "Core RPC remained blocked after shutdown"


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


def run_startup_error_case(action):
    import fcntl

    with tempfile.TemporaryDirectory(prefix="qml-startup-<b>-") as tmpdir:
        harness = QmlTestHarness(tmpdir=tmpdir, extra_args=["-regtest", "-disablewallet", "-qml_onboarded=0"])
        try:
            harness.start()
            gui = harness.driver
            walk_onboarding_to_connection(gui)
            network_dir = Path(harness.datadir) / "regtest"
            network_dir.mkdir(exist_ok=True)
            with (network_dir / ".lock").open("a+") as lock:
                fcntl.lockf(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                gui.click("onboardingConnectionButton")
                gui.wait_for_property("preInitStartupErrorPopup", "visible", True)
                message = gui.get_text("preInitStartupErrorMessage")
                assert "Cannot obtain a lock" in message, message
                assert "<b>" in message, message
                text_format = gui.get_property("preInitStartupErrorMessage", "textFormat")
                assert text_format in (0, "PlainText"), text_format
                if action == "acknowledge":
                    gui.click("preInitStartupErrorCloseButton")
                else:
                    gui.close_window()
                _, stderr = harness.process.communicate(timeout=30)
                assert harness.process.returncode == (1 if action == "acknowledge" else 0), stderr.decode("utf8", errors="replace")
        finally:
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
        gui.wait_for_property(object_name, "enabled", True)
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

                # Page visibility does not imply that its asynchronous preference
                # read has published. A disabled click owns no user command.
                gui.wait_for_property("windowBehaviorSettingsPage", "windowBehaviorModel.settingsReady", True)
                gui.wait_for_property("windowBehaviorSettingsPage", "windowBehaviorModel.persistencePending", False)
                assert gui.get_property("windowBehaviorSettingsPage", "windowBehaviorModel.persistenceError") in (None, "")
                # Write through the GUI so Qt chooses the platform's effective
                # organization and the redirected -test-settings-dir store.
                if phase == "save":
                    set_switch(gui, "showTrayIconSwitch", not hide_tray_icon)
                    set_switch(gui, "minimizeToTraySwitch", not hide_tray_icon)
                    set_switch(gui, "minimizeOnCloseSwitch", True)
                    gui.wait_for_property("windowBehaviorSettingsPage", "windowBehaviorModel.persistencePending", False)
                    assert gui.get_property("windowBehaviorSettingsPage", "windowBehaviorModel.persistenceError") in (None, "")
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
        try:
            if harness.process is not None and harness.process.poll() is None and harness.driver is not None:
                harness.driver.request_quit()
                harness.process.wait(timeout=10)
        finally:
            harness.stop(cleanup=False)
            if harness.process is not None:
                stdout, stderr = harness.process.communicate(timeout=5)
                print(f"First reset GUI exit: {harness.process.returncode}", flush=True)
                print("First reset GUI stdout:\n" + stdout.decode("utf8", errors="replace"), flush=True)
                print("First reset GUI stderr:\n" + stderr.decode("utf8", errors="replace"), flush=True)
                check_gui_exit(harness.process.returncode, stderr.decode("utf8", errors="replace"), socket_path=harness.socket_path)


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
        try:
            if harness.process is not None and harness.process.poll() is None and harness.driver is not None:
                harness.driver.request_quit()
                harness.process.wait(timeout=10)
        finally:
            harness.stop(cleanup=False)
            if harness.process is not None:
                stdout, stderr = harness.process.communicate(timeout=5)
                print(f"Second reset GUI exit: {harness.process.returncode}", flush=True)
                print("Second reset GUI stdout:\n" + stdout.decode("utf8", errors="replace"), flush=True)
                print("Second reset GUI stderr:\n" + stderr.decode("utf8", errors="replace"), flush=True)
                check_gui_exit(harness.process.returncode, stderr.decode("utf8", errors="replace"), socket_path=harness.socket_path)


def reset_onboarding_connection():
    with tempfile.TemporaryDirectory(prefix="qml_reset_") as directory:
        datadir = os.path.join(directory, "custom-data-dir")
        os.makedirs(datadir)
        run_first_reset_onboarding(directory, datadir)
        run_second_reset_onboarding(directory, datadir)


if __name__ == "__main__":
    raise SystemExit(run_scenarios('GUI startup and configuration process scenarios', [
        scenario("bridge-protocol", 1, bridge_smoke),
        scenario("unicode-startup-profile", 1, unicode_startup_profile),
        scenario("desktop-intent-primary-arguments", 8, desktop_intent_primary_arguments),
        scenario("desktop-intent-secondary-forwarding", 8, desktop_intent_secondary_forwarding),
        scenario("desktop-intent-secondary-forwarding-wallet-enabled", 8, desktop_intent_secondary_forwarding, wallet_enabled=True),
        scenario("desktop-intent-profile-isolation", 8, desktop_intent_profile_isolation),
        scenario("runtime-block-write-failure-exit-status", 4, runtime_block_write_failure_exit_status),
        scenario("onboarding", 2, onboarding_smoke),
        scenario("blocked-startup-finish", 2, run_case, "finish"),
        scenario("blocked-startup-quit", 2, run_case, "quit"),
        scenario("blocked-startup-close", 2, run_case, "close"),
        scenario("startup-error-acknowledge", 2, run_startup_error_case, "acknowledge"),
        scenario("startup-error-close", 2, run_startup_error_case, "close"),
        scenario("disablewallet-cli", 2, boot_mode, ["-disablewallet"]),
        scenario("disablewallet-config", 2, boot_mode, [], config_disabled=True),
        scenario("disablewallet-settings", 2, boot_mode, [], settings_disabled=True),
        scenario("reset-window-with-tray", 2, run_window_behavior_reset, hide_tray_icon=False),
        scenario("reset-window-without-tray", 2, run_window_behavior_reset, hide_tray_icon=True),
        scenario("reset-onboarding-connection", 2, reset_onboarding_connection),
        scenario("wallet-cli-override", 4, boot_mode, ["-disablewallet=0"], config_disabled=True, wallet_enabled=True),
    ]))
