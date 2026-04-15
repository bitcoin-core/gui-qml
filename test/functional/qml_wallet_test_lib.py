#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Shared helpers for QML wallet flow tests."""

import base64
import http.client
import json
import os
import shutil
import signal
import socket
import subprocess
import tempfile
import time

from qml_driver import QmlDriver
from qml_test_harness import GUI_STARTUP_TIMEOUT, complete_onboarding, find_gui_binary


RPC_USER = "qmlwallettest"
RPC_PASS = "qmlwallettestpass"


def find_bitcoind():
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
    default = os.path.join(repo_root, "build", "bin", "bitcoind")
    path = os.getenv("BITCOIND", default)
    if not os.path.isfile(path):
        raise FileNotFoundError(
            f"bitcoind not found at {path}. "
            "Build it with -DBUILD_DAEMON=ON or set the BITCOIND environment variable."
        )
    return path


def find_legacy_bitcoind():
    explicit = os.getenv("BITCOIND_LEGACY")
    if explicit and os.path.isfile(explicit):
        return explicit

    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
    releases_root = os.getenv("PREVIOUS_RELEASES_DIR", os.path.join(repo_root, "releases"))
    candidates = [
        os.path.join(releases_root, "v28.0", "bin", "bitcoind"),
        os.path.join(releases_root, "v27.0", "bin", "bitcoind"),
    ]
    for candidate in candidates:
        if os.path.isfile(candidate):
            return candidate

    return None


def pick_unused_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        return sock.getsockname()[1]


def rpc_call(port, method, params=None, wallet=None):
    payload = json.dumps({
        "jsonrpc": "1.0",
        "id": "qml_wallet_test",
        "method": method,
        "params": params or [],
    }).encode("utf-8")

    path = "/"
    if wallet:
        path = f"/wallet/{wallet}"

    conn = http.client.HTTPConnection("127.0.0.1", port, timeout=60)
    credentials = base64.b64encode(f"{RPC_USER}:{RPC_PASS}".encode("utf-8")).decode("ascii")
    conn.request(
        "POST",
        path,
        body=payload,
        headers={
            "Content-Type": "application/json",
            "Authorization": f"Basic {credentials}",
        },
    )
    response = conn.getresponse()
    body = json.loads(response.read())
    conn.close()
    if body.get("error"):
        raise RuntimeError(body["error"]["message"])
    return body["result"]


def wait_for_rpc(port, timeout=30):
    deadline = time.time() + timeout
    last_error = None
    while time.time() < deadline:
        try:
            rpc_call(port, "getblockcount")
            return
        except Exception as err:  # noqa: BLE001 - test helper should retry on any startup error
            last_error = err
            time.sleep(0.25)
    raise RuntimeError(f"RPC on port {port} did not become ready: {last_error}")


def write_datadir(datadir, rpc_port, p2p_port, extra_lines=None):
    os.makedirs(datadir, exist_ok=True)
    conf_path = os.path.join(datadir, "bitcoin.conf")
    with open(conf_path, "w", encoding="utf8") as conf:
        conf.write("regtest=1\n")
        conf.write("[regtest]\n")
        conf.write("server=1\n")
        conf.write(f"rpcuser={RPC_USER}\n")
        conf.write(f"rpcpassword={RPC_PASS}\n")
        conf.write(f"rpcport={rpc_port}\n")
        conf.write(f"port={p2p_port}\n")
        conf.write("discover=0\n")
        conf.write("dnsseed=0\n")
        conf.write("fixedseeds=0\n")
        conf.write("listenonion=0\n")
        conf.write("printtoconsole=0\n")
        conf.write("connect=0\n")
        conf.write("listen=0\n")
        conf.write("shrinkdebugfile=0\n")
        conf.write("fallbackfee=0.0001\n")
        if extra_lines:
            for line in extra_lines:
                conf.write(f"{line}\n")


def update_settings_json(datadir, updates):
    settings_dir = os.path.join(datadir, "regtest")
    if not os.path.isdir(settings_dir):
        settings_dir = datadir
    os.makedirs(settings_dir, exist_ok=True)
    settings_path = os.path.join(settings_dir, "settings.json")
    settings = {}
    if os.path.exists(settings_path):
        with open(settings_path, "r", encoding="utf8") as settings_file:
            settings = json.load(settings_file)

    for key, value in updates.items():
        if value is None:
            settings.pop(key, None)
        else:
            settings[key] = value

    with open(settings_path, "w", encoding="utf8") as settings_file:
        json.dump(settings, settings_file, indent=4, sort_keys=True)
        settings_file.write("\n")


class WalletFlowHarness:
    """Launches a source bitcoind and the QML GUI with isolated datadirs."""

    def __init__(self, name, port_offset):
        self.name = name
        self.port_offset = port_offset
        self.gui_binary = find_gui_binary()
        self.bitcoind_binary = None
        self.tmpdir = tempfile.mkdtemp(prefix=f"{name}_")
        self.socket_path = os.path.join(self.tmpdir, "test_bridge.sock")
        self.gui_datadir = os.path.join(self.tmpdir, "gui_node")
        self.source_datadir = os.path.join(self.tmpdir, "source_node")
        self.config_home = os.path.join(self.tmpdir, "config")
        self.gui_process = None
        self.source_process = None
        self.driver = None
        self.gui_p2p_port = pick_unused_port()
        self.gui_rpc_port = pick_unused_port()
        self.source_p2p_port = pick_unused_port()
        self.source_rpc_port = pick_unused_port()

        write_datadir(self.gui_datadir, self.gui_rpc_port, self.gui_p2p_port)
        write_datadir(self.source_datadir, self.source_rpc_port, self.source_p2p_port)

    @property
    def gui_wallets_path(self):
        return os.path.join(self.gui_datadir, "regtest", "wallets")

    @property
    def gui_settings_path(self):
        return os.path.join(self.gui_datadir, "regtest", "settings.json")

    @property
    def source_wallets_path(self):
        return os.path.join(self.source_datadir, "regtest", "wallets")

    def start_source_node(self, extra_args=None, binary=None):
        bitcoind_binary = binary or self.bitcoind_binary or find_bitcoind()
        self.bitcoind_binary = bitcoind_binary
        args = [bitcoind_binary, f"-datadir={self.source_datadir}"]
        if extra_args:
            args.extend(extra_args)
        self.source_process = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        wait_for_rpc(self.source_rpc_port)

    def stop_source_node(self):
        if self.source_process and self.source_process.poll() is None:
            try:
                rpc_call(self.source_rpc_port, "stop")
            except Exception:
                self.source_process.send_signal(signal.SIGTERM)
            try:
                self.source_process.wait(timeout=20)
            except subprocess.TimeoutExpired:
                self.source_process.kill()
                self.source_process.wait()
        self.source_process = None

    def start_gui(self, reset_gui_settings=False, extra_args=None, cwd=None):
        env = dict(os.environ)
        env["QT_QPA_PLATFORM"] = "offscreen"
        os.makedirs(self.config_home, exist_ok=True)
        env["XDG_CONFIG_HOME"] = self.config_home
        args = [
            self.gui_binary,
            f"-datadir={self.gui_datadir}",
            f"-test-automation={self.socket_path}",
            "-logtimemicros",
            "-debug",
            "-debugexclude=libevent",
            "-debugexclude=leveldb",
            "-nolisten",
        ]
        if reset_gui_settings:
            args.insert(3, "-resetguisettings")
        if extra_args:
            args.extend(extra_args)
        self.gui_process = subprocess.Popen(
            args,
            env=env,
            cwd=cwd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        self.driver = QmlDriver(self.socket_path, timeout=GUI_STARTUP_TIMEOUT)

    def stop_gui(self):
        if self.gui_process and self.gui_process.poll() is None:
            self.gui_process.send_signal(signal.SIGTERM)
            try:
                self.gui_process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.gui_process.kill()
                self.gui_process.wait()
        self.gui_process = None
        if self.driver:
            self.driver.close()
            self.driver = None

    def restart_gui(self, reset_gui_settings=False, extra_args=None, cwd=None):
        self.stop_gui()
        self.start_gui(reset_gui_settings=reset_gui_settings, extra_args=extra_args, cwd=cwd)

    def update_gui_settings(self, updates):
        update_settings_json(self.gui_datadir, updates)

    def stop(self):
        self.stop_source_node()
        self.stop_gui()
        if os.getenv("KEEP_QML_TEST_TMPDIR") == "1":
            print(f"Preserving test directory: {self.tmpdir}")
        else:
            shutil.rmtree(self.tmpdir, ignore_errors=True)

    def process_output(self, process):
        if not process:
            return ""
        stdout = process.stdout.read().decode("utf-8", errors="replace") if process.stdout else ""
        stderr = process.stderr.read().decode("utf-8", errors="replace") if process.stderr else ""
        if stdout and stderr:
            return f"stdout:\n{stdout}\n\nstderr:\n{stderr}"
        return stdout or stderr

    def finish_onboarding(self):
        complete_onboarding(self.driver)
