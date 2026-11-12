#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test QML shutdown while load_on_startup wallets are loading."""

import os
import re
import signal
import subprocess
import time
import unittest
from pathlib import Path
from qml_driver import QmlDriverError
from qml_process_checks import (
    check_gui_exit,
    report_gui_failure,
    unexpected_gui_stderr,
)
from qml_scenarios import (
    run_scenarios,
    scenario,
)
from qml_test_harness import (
    QmlTestHarness,
    qml_qpa_platform,
    qsettings_sandbox_args,
)
from qml_wallet_test_lib import (
    WalletFlowHarness,
    find_bitcoind,
    rpc_call,
    wait_for_rpc,
)


STARTUP_WALLET_COUNT = 8
STARTUP_KEYPOOL_SIZE = 100
SHUTDOWN_TIMEOUT_SECS = 30


def start_node(datadir):
    process = subprocess.Popen(
        [find_bitcoind(), f"-datadir={datadir}"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    return process


def stop_node(process, rpc_port):
    if process.poll() is not None:
        return
    try:
        rpc_call(rpc_port, "stop")
    except Exception:
        process.send_signal(signal.SIGTERM)
    try:
        process.wait(timeout=SHUTDOWN_TIMEOUT_SECS)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()


def create_load_on_startup_wallets(harness, prefix):
    # Keep eight real startup loads, without deriving the default 8,000 keys
    # per wallet. Under sanitizers that work can exceed the shutdown timeout.
    with open(os.path.join(harness.gui_datadir, "bitcoin.conf"), "a", encoding="utf8") as conf:
        conf.write(f"keypool={STARTUP_KEYPOOL_SIZE}\n")
    process = start_node(harness.gui_datadir)
    try:
        wait_for_rpc(harness.gui_rpc_port)
        for index in range(STARTUP_WALLET_COUNT):
            rpc_call(
                harness.gui_rpc_port,
                "createwallet",
                {
                    "wallet_name": f"{prefix}_{index}",
                    "load_on_startup": True,
                },
            )
    finally:
        stop_node(process, harness.gui_rpc_port)


def wait_for_process_exit(process, description, socket_path):
    deadline = time.time() + SHUTDOWN_TIMEOUT_SECS
    while time.time() < deadline:
        if process.poll() is not None:
            _, stderr = process.communicate()
            check_gui_exit(process.returncode, stderr.decode("utf-8", errors="replace"), socket_path=socket_path)
            return process.returncode
        time.sleep(0.25)
    raise TimeoutError(f"GUI did not exit after {description}")


def debug_log_path(harness):
    return os.path.join(harness.gui_datadir, "regtest", "debug.log")


def read_debug_log(harness):
    path = debug_log_path(harness)
    if not os.path.isfile(path):
        return ""
    with open(path, "r", encoding="utf8", errors="replace") as log_file:
        return log_file.read()


def wait_for_startup_initialization(harness, process, baseline_wallet_loads_completed):
    deadline = time.time() + SHUTDOWN_TIMEOUT_SECS
    initialization_seen_at = None
    while time.time() < deadline:
        if process.poll() is not None:
            raise RuntimeError(
                f"GUI exited before startup could be interrupted, return code {process.returncode}"
            )

        debug_log = read_debug_log(harness)
        wallet_loads_completed = debug_log.count("Wallet completed loading")
        if (
            baseline_wallet_loads_completed
            < wallet_loads_completed
            < baseline_wallet_loads_completed + STARTUP_WALLET_COUNT
        ):
            return

        if "Running initialization in thread" in debug_log:
            initialization_seen_at = initialization_seen_at or time.time()
            if time.time() - initialization_seen_at >= 0.2:
                return

        time.sleep(0.05)

    raise TimeoutError("Timed out waiting for GUI startup initialization")


def start_gui_without_driver(harness):
    env = dict(os.environ)
    env["QT_QPA_PLATFORM"] = qml_qpa_platform()
    settings_args = qsettings_sandbox_args(env, harness.config_home)
    args = [
        harness.gui_binary,
        f"-datadir={harness.gui_datadir}",
        f"-test-automation={harness.socket_path}",
    ] + settings_args + [
        "-qml_onboarded=1",
        "-logtimemicros",
        "-debug",
        "-debugexclude=leveldb",
        "-nolisten",
    ]
    harness.gui_process = subprocess.Popen(
        args,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )


def case_close_preinit_onboarding(native_quit=False):
    harness = QmlTestHarness(
        use_datadir_arg=False,
        reset_settings=True,
        start_onboarded=False,
        extra_args=["-regtest"],
    )
    try:
        harness.start()
        harness.driver.wait_for_page("onboardingCover", timeout_ms=5000)
        if native_quit:
            harness.driver.request_quit()
        else:
            harness.driver.close_window()
        wait_for_process_exit(harness.process, "closing pre-init onboarding", harness.socket_path)
    except BaseException:
        report_gui_failure(harness.process, "closing pre-init onboarding")
        raise
    finally:
        harness.stop()


def case_close_window_during_load_on_startup(native_quit=False):
    harness = WalletFlowHarness("qml_shutdown_close", 970)
    try:
        create_load_on_startup_wallets(harness, "shutdown_close")
        baseline_wallet_loads_completed = read_debug_log(harness).count("Wallet completed loading")
        harness.start_gui()
        wait_for_startup_initialization(harness, harness.gui_process, baseline_wallet_loads_completed)

        gui = harness.driver
        if native_quit:
            gui.request_quit()
        else:
            gui.close_window()
        gui.wait_for_page("shutdownPage", timeout_ms=5000)

        return_code = wait_for_process_exit(harness.gui_process, "closing during load_on_startup", harness.socket_path)
        assert return_code == 0, f"Expected GUI exit code 0, got {return_code}"
    except BaseException:
        report_gui_failure(harness.gui_process, "closing during load_on_startup")
        raise
    finally:
        harness.stop()


def case_sigint_during_load_on_startup():
    harness = WalletFlowHarness("qml_shutdown_sigint", 980)
    try:
        create_load_on_startup_wallets(harness, "shutdown_sigint")
        baseline_wallet_loads_completed = read_debug_log(harness).count("Wallet completed loading")
        start_gui_without_driver(harness)

        wait_for_startup_initialization(harness, harness.gui_process, baseline_wallet_loads_completed)
        harness.gui_process.send_signal(signal.SIGINT)

        return_code = wait_for_process_exit(harness.gui_process, "SIGINT during load_on_startup", harness.socket_path)
        assert return_code == 0, f"Expected GUI exit code 0 after SIGINT, got {return_code}"
    except BaseException:
        report_gui_failure(harness.gui_process, "SIGINT during load_on_startup")
        raise
    finally:
        harness.stop()


class FatalShutdownTests(unittest.TestCase):
    def check_fatal_exit(self, phase, acknowledgement):
        harness = WalletFlowHarness("qml_fatal_shutdown", 0)
        try:
            harness.start_gui(extra_args=["-disablewallet", f"-test-fatal-exception={phase}"])
            gui = harness.driver
            wait_for_rpc(harness.gui_rpc_port)
            self.assertEqual(rpc_call(harness.gui_rpc_port, "getblockchaininfo")["chain"], "regtest")
            if phase != "initialize":
                gui.close_window()
                gui.wait_for_page("shutdownPage", timeout_ms=10000)
            gui.wait_for_property("nodeFatalErrorPopup", "opened", True, timeout_ms=10000)
            message = {
                "initialize": "Test fatal exception after initialization",
                "interrupt": "Test fatal exception during interruption",
                "shutdown": "Test fatal exception before shutdown",
            }[phase]
            self.assertIn(message, gui.get_text("nodeFatalErrorText"))
            self.assertTrue(gui.get_property("nodeFatalShutdownButton", "enabled"))
            self.assertTrue(gui.get_property("nodeFatalShutdownButton", "visible"))
            self.assertFalse(gui.get_property("mainPageStack", "enabled"))
            try:
                if acknowledgement == "quit":
                    gui.request_quit()
                elif acknowledgement == "close":
                    gui.close_window()
                else:
                    gui.click("nodeFatalShutdownButton")
            except (QmlDriverError, ConnectionError):
                # Immediate process exit can close the bridge before its reply.
                pass
            _, stderr = harness.gui_process.communicate(timeout=10)
            stderr = stderr.decode("utf-8", errors="replace")
            self.assertEqual(harness.gui_process.returncode, 1, stderr)
            debug_log = (Path(harness.gui_datadir) / "regtest" / "debug.log").read_text(encoding="utf-8")
            self.assertIn(message, debug_log)
            self.assertIn("Runaway exception", debug_log)
            # Remove only the expected injected exception; other diagnostics still fail.
            stderr, count = re.subn(
                r"\n\n\*{24}\nEXCEPTION: [^\n]+\n" + re.escape(message)
                + r" +\n[^\n]+ in Runaway exception +\n\n",
                "", stderr, count=1,
            )
            self.assertEqual(count, 1, stderr)
            self.assertEqual(unexpected_gui_stderr(stderr, socket_path=harness.socket_path), [])
        except BaseException:
            report_gui_failure(harness.gui_process, f"fatal {phase}")
            raise
        finally:
            harness.stop()

if __name__ == "__main__":
    raise SystemExit(run_scenarios('GUI normal, interrupted startup and fatal process exits', [
        scenario("onboarding-close", 2, case_close_preinit_onboarding),
        scenario("onboarding-quit", 2, case_close_preinit_onboarding, native_quit=True),
        scenario("wallet-loading-close", 4, case_close_window_during_load_on_startup),
        scenario("wallet-loading-quit", 4, case_close_window_during_load_on_startup, native_quit=True),
        scenario("wallet-loading-sigint", 4, case_sigint_during_load_on_startup),
    ] + [
        scenario(f"fatal-{phase}-{acknowledgement}", 1,
                 FatalShutdownTests().check_fatal_exit, phase, acknowledgement)
        for phase in ("initialize", "interrupt", "shutdown")
        for acknowledgement in ("button", "quit", "close")
    ]))
