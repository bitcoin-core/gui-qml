#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Shared test harness for QML test automation.

Provides QmlTestHarness which launches bitcoin-core-app with the test
bridge enabled and connects a QmlDriver instance.
"""

import argparse
import os
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time

from qml_driver import QmlDriver, QmlDriverError


# How long to wait for the GUI process to start (seconds).
GUI_STARTUP_TIMEOUT = 30


def pick_unused_port():
    """Return an available TCP port on 127.0.0.1."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        return s.getsockname()[1]


def find_gui_binary():
    """Locate the bitcoin-core-app binary.

    Search order:
      1. BITCOIN_CORE_APP environment variable
      2. <repo_root>/build/bin/bitcoin-core-app
    """
    env_path = os.getenv("BITCOIN_CORE_APP")
    if env_path and os.path.isfile(env_path):
        return env_path

    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
    build_path = os.path.join(repo_root, 'build', 'bin', 'bitcoin-core-app')
    if os.path.isfile(build_path):
        return build_path

    raise FileNotFoundError(
        "Cannot find bitcoin-core-app binary. "
        "Set BITCOIN_CORE_APP env var or build with -DENABLE_TEST_AUTOMATION=ON."
    )


def setup_datadir(tmpdir, rpc_port=None):
    """Create a minimal regtest data directory with bitcoin.conf."""
    datadir = os.path.join(tmpdir, "node0")
    os.makedirs(datadir, exist_ok=True)
    conf_path = os.path.join(datadir, "bitcoin.conf")
    with open(conf_path, "w", encoding="utf8") as f:
        f.write("regtest=1\n")
        f.write("[regtest]\n")
        f.write("server=1\n")
        if rpc_port is not None:
            f.write(f"rpcport={rpc_port}\n")
        f.write("rpcbind=127.0.0.1\n")
        f.write("rpcallowip=127.0.0.1\n")
        f.write("discover=0\n")
        f.write("dnsseed=0\n")
        f.write("fixedseeds=0\n")
        f.write("listenonion=0\n")
        f.write("printtoconsole=0\n")
        f.write("connect=0\n")
        f.write("shrinkdebugfile=0\n")
        f.write("fallbackfee=0.0001\n")
    return datadir


def qsettings_sandbox_args(env, config_home):
    """Configure a process environment and CLI args for isolated QSettings."""
    os.makedirs(config_home, exist_ok=True)
    env["XDG_CONFIG_HOME"] = config_home
    return [f"-test-settings-dir={config_home}"]


def qml_qpa_platform():
    """Return the Qt platform plugin for QML functional tests."""
    return os.getenv("QML_TEST_QPA_PLATFORM", os.getenv("QT_QPA_PLATFORM", "offscreen"))


def has_cli_arg(args, name):
    """Return whether args includes a command-line option by exact name."""
    prefix = f"{name}="
    return any(arg == name or arg.startswith(prefix) for arg in args)


def qml_onboarded_args(extra_args, reset_settings=False, start_onboarded=True):
    """Return hidden args that make managed tests start in the runtime shell."""
    extra_args = extra_args or []
    if (
        not start_onboarded
        or reset_settings
        or has_cli_arg(extra_args, "-resetguisettings")
        or has_cli_arg(extra_args, "-choosedatadir")
        or has_cli_arg(extra_args, "-qml_onboarded")
    ):
        return []
    return ["-qml_onboarded=1"]


def parse_args():
    """Parse common CLI arguments for QML test scripts."""
    parser = argparse.ArgumentParser(
        description="QML test automation",
        add_help=True,
    )
    parser.add_argument(
        "--socket-path",
        help="Connect to an already-running bitcoin-core-app instance at "
             "this Unix socket path instead of launching a new one.  "
             "Start the app with: bitcoin-core-app -test-automation=<path>",
    )
    return parser.parse_args()


class QmlTestHarness:
    """Test harness that launches the GUI and connects the test bridge.

    If socket_path is provided, attaches to an already-running instance
    instead of launching a new one.
    """

    def __init__(self, socket_path=None, extra_args=None, reset_settings=False, start_onboarded=True, datadir=None, use_datadir_arg=True, tmpdir=None, no_listen_arg=True):
        self.external = socket_path is not None
        self.process = None
        self.driver = None
        self.extra_args = extra_args or []
        self.reset_settings = reset_settings
        self.start_onboarded = start_onboarded
        self.use_datadir_arg = use_datadir_arg
        self.no_listen_arg = no_listen_arg
        self.config_home = None
        self.home_dir = None
        self._owns_tmpdir = tmpdir is None

        if self.external:
            self.socket_path = socket_path
            self.tmpdir = None
            self.datadir = None
            self.rpc_port = None
        else:
            self.gui_binary = find_gui_binary()
            if tmpdir is not None:
                self.tmpdir = tmpdir
            elif datadir is None:
                self.tmpdir = tempfile.mkdtemp(prefix="qml_test_bridge_")
            else:
                self.tmpdir = None

            if datadir is not None:
                # Reuse an existing datadir; don't create or delete a tmpdir.
                self.datadir = datadir
                self.socket_path = os.path.join(datadir, "test_bridge.sock")
                self.config_home = os.path.join(os.path.dirname(datadir), "config")
                self.rpc_port = None
                self.home_dir = os.path.join(os.path.dirname(datadir), "home")
            else:
                self.rpc_port = pick_unused_port()
                self.datadir = setup_datadir(self.tmpdir, rpc_port=self.rpc_port) if use_datadir_arg else None
                self.socket_path = os.path.join(self.tmpdir, "test_bridge.sock")
                self.config_home = os.path.join(self.tmpdir, "config")
                self.home_dir = os.path.join(self.tmpdir, "home")

    def start(self):
        """Launch bitcoin-core-app or attach to an existing instance."""
        if self.external:
            print(f"Connecting to existing GUI at {self.socket_path} ...")
            self.driver = QmlDriver(self.socket_path, timeout=GUI_STARTUP_TIMEOUT)
            print("QmlDriver connected to test bridge.")
            return

        env = dict(os.environ)
        env["QT_QPA_PLATFORM"] = qml_qpa_platform()
        settings_args = []
        if self.config_home:
            settings_args = qsettings_sandbox_args(env, self.config_home)
        if self.home_dir:
            os.makedirs(self.home_dir, exist_ok=True)
            env["HOME"] = self.home_dir

        args = [
            self.gui_binary,
            f"-test-automation={self.socket_path}",
        ] + settings_args + (["-resetguisettings"] if self.reset_settings else []) + qml_onboarded_args(
            self.extra_args,
            reset_settings=self.reset_settings,
            start_onboarded=self.start_onboarded,
        ) + [
            "-logtimemicros",
            "-debug",
            "-debugexclude=leveldb",
        ] + self.extra_args
        if self.no_listen_arg:
            args.append("-nolisten")
        if self.use_datadir_arg:
            args.insert(1, f"-datadir={self.datadir}")

        print(f"Starting GUI: {' '.join(args)}")
        self.process = subprocess.Popen(
            args,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

        # Connect the QmlDriver (retries internally until the socket appears).
        try:
            self.driver = QmlDriver(self.socket_path, timeout=GUI_STARTUP_TIMEOUT)
        except QmlDriverError:
            self._dump_startup_failure_context()
            raise
        print("QmlDriver connected to test bridge.")

    def process_output(self):
        """Return captured stdout+stderr from the GUI process as a string."""
        if not self.process:
            return ""
        if self.process.poll() is None:
            return ""
        stdout = (
            self.process.stdout.read().decode("utf-8", errors="replace")
            if self.process.stdout else ""
        )
        stderr = (
            self.process.stderr.read().decode("utf-8", errors="replace")
            if self.process.stderr else ""
        )
        parts = []
        if stdout:
            parts.append(f"stdout:\n{stdout}")
        if stderr:
            parts.append(f"stderr:\n{stderr}")
        return "\n\n".join(parts)

    def stop(self, cleanup=True):
        """Shut down the GUI process (only if we launched it).

        If cleanup is False, the tmpdir and datadir are preserved on disk so
        a second harness can reuse the same datadir for a restart-persistence test.
        """
        if self.process and self.process.poll() is None:
            self.process.send_signal(signal.SIGTERM)
            try:
                self.process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
        if self.driver:
            self.driver.close()
        if cleanup and self.tmpdir and self._owns_tmpdir:
            shutil.rmtree(self.tmpdir, ignore_errors=True)
            self.tmpdir = None

    def _candidate_debug_logs(self):
        if not self.datadir:
            return []
        return [
            os.path.join(self.datadir, "regtest", "debug.log"),
            os.path.join(self.datadir, "debug.log"),
        ]

    def _dump_startup_failure_context(self):
        print("\n--- GUI startup failure context ---", file=sys.stderr)
        print(f"Expected test bridge socket: {self.socket_path}", file=sys.stderr)

        if not self.process:
            print("GUI process was not launched.", file=sys.stderr)
            return

        return_code = self.process.poll()
        if return_code is None:
            print(
                f"GUI process is still running (pid={self.process.pid}) but the bridge socket never appeared.",
                file=sys.stderr,
            )
        else:
            print(
                f"GUI process exited before the bridge connected with return code {return_code}.",
                file=sys.stderr,
            )
            stdout, stderr = self.process.communicate(timeout=1)
            if stdout:
                print("\n--- GUI stdout ---", file=sys.stderr)
                print(stdout.decode("utf-8", errors="replace"), file=sys.stderr)
            if stderr:
                print("\n--- GUI stderr ---", file=sys.stderr)
                print(stderr.decode("utf-8", errors="replace"), file=sys.stderr)

        for debug_log in self._candidate_debug_logs():
            if os.path.isfile(debug_log):
                print(f"\n--- debug.log: {debug_log} ---", file=sys.stderr)
                with open(debug_log, "r", encoding="utf8", errors="replace") as fh:
                    print(fh.read(), file=sys.stderr)
                break
        else:
            print("\n--- debug.log not found ---", file=sys.stderr)

    def reconnect(self, timeout=GUI_STARTUP_TIMEOUT):
        """Reconnect the driver to a new bridge on the same socket path."""
        if self.driver:
            self.driver.reconnect(timeout=timeout)
        else:
            self.driver = QmlDriver(self.socket_path, timeout=timeout)
        return self.driver

    def wait_for_main_window_reconnect(self, timeout=GUI_STARTUP_TIMEOUT):
        """Reconnect after the pre-init bridge is destroyed and main bridge starts."""
        deadline = time.time() + timeout
        last_error = None
        while time.time() < deadline:
            try:
                gui = self.reconnect(timeout=1)
                gui.wait_for_object("mainPageStack", timeout_ms=1000)
                return gui
            except Exception as err:
                last_error = err
                time.sleep(0.25)
        raise QmlDriverError(f"Could not reconnect to main QML window: {last_error}")


def walk_onboarding_to_connection(gui):
    """Click through onboarding pages up to the connection page."""
    gui.wait_for_page("onboardingCover", timeout_ms=10000)
    steps = [
        ("onboardingCoverButton",           "onboardingStrengthen"),
        ("onboardingStrengthenButton",      "onboardingBlockchain"),
        ("onboardingBlockchainButton",      "onboardingBlockclock"),
        ("onboardingBlockclockButton",      "onboardingStorageLocation"),
        ("onboardingStorageLocationButton", "onboardingStorageAmount"),
        ("onboardingStorageAmountButton",   "onboardingConnection"),
    ]
    for button, expected_page in steps:
        gui.wait_for_property(button, "enabled", True)
        gui.click(button)
        gui.wait_for_page(expected_page, timeout_ms=5000)


def complete_visible_onboarding(gui):
    """Finish onboarding if it is visible on the current QML engine.

    Managed runtime tests should start with qml_onboarded=true and treat this
    as a no-op. Full pre-init onboarding destroys the first bridge and must use
    complete_preinit_onboarding() followed by wait_for_main_window_reconnect().
    """
    if not gui.object_exists("onboardingCover"):
        return
    if gui.object_exists("preInitWindow"):
        raise QmlDriverError(
            "Connected to pre-init onboarding; use complete_preinit_onboarding() "
            "and QmlTestHarness.wait_for_main_window_reconnect() instead."
        )
    walk_onboarding_to_connection(gui)
    gui.click("onboardingConnectionButton")
    time.sleep(1)  # Allow navigation to the post-onboarding screen to settle.


def complete_preinit_onboarding(gui):
    """Finish full pre-init onboarding; caller must reconnect afterwards."""
    walk_onboarding_to_connection(gui)
    gui.click("onboardingConnectionButton")


def complete_onboarding(gui):
    """Backward-compatible alias for existing tests connected to one engine."""
    complete_visible_onboarding(gui)


def assert_wallet_shell_visible(gui, timeout_ms=30000):
    """Assert the desktop wallet shell is the main post-onboarding UI."""
    gui.wait_for_object("mainPageStack", timeout_ms=timeout_ms)
    gui.wait_for_property("walletBadge", "visible", True, timeout_ms=timeout_ms)
    gui.wait_for_property("walletBadge", "loading", False, timeout_ms=timeout_ms)
    assert gui.object_exists("walletBadge"), "Expected walletBadge in wallet shell"
    assert not gui.object_exists("nodeRunner"), "Expected wallet shell, but nodeRunner is present"


def assert_onboarding_wallet_creation_visible(gui, timeout_ms=30000):
    """Assert setup finished on the node tab with wallet creation open as a modal."""
    gui.wait_for_object("mainPageStack", timeout_ms=timeout_ms)
    gui.wait_for_object("walletBadge", timeout_ms=timeout_ms)
    gui.wait_for_property("walletCreationModal", "visible", True, timeout_ms=timeout_ms)
    gui.wait_for_object("createWalletWizard", timeout_ms=timeout_ms)
    gui.wait_for_page("walletCreationTypePage", timeout_ms=timeout_ms)
    assert gui.object_exists("walletBadge"), "Expected wallet shell behind create wallet wizard"
    assert gui.get_property("blockClockTabButton", "checked"), "Expected node tab behind wallet creation"
    assert not gui.object_exists("nodeRunner"), "Expected wallet onboarding flow, but nodeRunner is present"


def assert_node_shell_visible(gui, timeout_ms=30000):
    """Assert the node-only shell is the main post-onboarding UI."""
    gui.wait_for_object("mainPageStack", timeout_ms=timeout_ms)
    gui.wait_for_property("nodeRunner", "visible", True, timeout_ms=timeout_ms)
    assert gui.object_exists("nodeRunner"), "Expected nodeRunner in node shell"
    assert not gui.object_exists("walletBadge"), "Did not expect walletBadge when wallet is disabled"


def dump_qml_tree(driver):
    """Print the full QML object tree for debugging."""
    try:
        print("\n--- QML object tree at failure ---")
        all_objects = driver.list_objects()
        for obj in all_objects:
            print(f"  {obj['objectName']} ({obj['className']})")
        print(f"--- {len(all_objects)} objects total ---")
    except Exception:
        print("  (could not retrieve object tree)")
