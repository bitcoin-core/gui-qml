#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""End-to-end tests for peer management: disconnect, ban, and unban.

Starts the GUI (bitcoin-core-app) as a regtest node that listens for
inbound connections, then starts a second bitcoind that connects to it.
The test drives the GUI through the Peers page to exercise each action
and verifies the result via JSON-RPC.

This test requires:
  - bitcoin-core-app built with -DENABLE_TEST_AUTOMATION=ON
  - bitcoind binary (searched alongside bitcoin-core-app, in build/bin/,
    in a sibling 'bitcoin' repo, or set BITCOIND env var)
"""

import base64
import http.client
import json
import os
import signal
import socket
import subprocess
import sys
import tempfile
import time

from qml_test_harness import (
    GUI_STARTUP_TIMEOUT,
    dump_qml_tree,
    find_gui_binary,
)
from qml_driver import QmlDriver


# ── Port allocation ───────────────────────────────────────────────────────────
# Ports are laid out as consecutive pairs (P2P, RPC) per node, indexed from 0:
#   Node 0 (GUI):   P2P = base+0, RPC = base+1
#   Node 1 (peer):  P2P = base+2, RPC = base+3
#   Node 2 (peer2): P2P = base+4, RPC = base+5
#
# Set TEST_RUNNER_PORT_MIN to a different base to avoid conflicts when running
# multiple test instances on the same machine (e.g. in parallel CI jobs).
_PORT_BASE_DEFAULT = 18555

def _p2p_port(node_idx: int) -> int:
    base = int(os.getenv("TEST_RUNNER_PORT_MIN", _PORT_BASE_DEFAULT))
    return base + node_idx * 2

def _rpc_port(node_idx: int) -> int:
    return _p2p_port(node_idx) + 1

_COMMON_CONF = (
    "discover=0\n"
    "dnsseed=0\n"
    "fixedseeds=0\n"
    "listenonion=0\n"
    "printtoconsole=0\n"
    "shrinkdebugfile=0\n"
)

def _terminate_process(proc) -> None:
    if proc and proc.poll() is None:
        proc.send_signal(signal.SIGTERM)
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=5)

GUI_RPC_USER = "qmltest"
GUI_RPC_PASS = "qmltestpass"

BAN_DURATIONS = [
    (3600,      "1 hour"),
    (86400,     "1 day"),
    (604800,    "1 week"),
    (31536000,  "1 year"),
]

PEER_LIST_ITEM_VISIBLE_TIMEOUT_MS = 10000
PEER_ACTION_TIMEOUT_SECS          = 30
BAN_RPC_APPLY_TIMEOUT_SECS        = 10


# ── Helper: port readiness ────────────────────────────────────────────────────

def _wait_for_port(host, port, timeout=10):
    """Block until a TCP port accepts connections or timeout expires."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with socket.create_connection((host, port), timeout=1):
                return
        except OSError:
            time.sleep(0.2)
    raise RuntimeError(f"Port {host}:{port} did not open within {timeout}s")


# ── Helper: find bitcoind ─────────────────────────────────────────────────────

def find_bitcoind():
    """Locate the bitcoind binary.

    Checks the BITCOIND environment variable first; falls back to
    build/bin/bitcoind relative to the repo root.
    """
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
    default = os.path.join(repo_root, 'build', 'bin', 'bitcoind')
    path = os.getenv('BITCOIND', default)
    if not os.path.isfile(path):
        raise FileNotFoundError(
            f"bitcoind not found at {path}. "
            "Build it alongside the app with -DBUILD_DAEMON=ON, or set the BITCOIND environment variable."
        )
    return path


# ── Harness ───────────────────────────────────────────────────────────────────

class PeerQmlTestHarness:
    """Launches the GUI node and a peer bitcoind, connects them, and provides
    a QmlDriver for test automation plus an rpc_call() helper for verification.
    """

    def __init__(self):
        self.gui_binary = find_gui_binary()
        self.bitcoind_binary = find_bitcoind()
        self.tmpdir = tempfile.mkdtemp(prefix="qml_test_peers_")
        self.socket_path = os.path.join(self.tmpdir, "test_bridge.sock")
        self.gui_process = None
        self.peer_process = None
        self.peer2_process = None
        self.driver = None

        self.gui_datadir = self._setup_gui_datadir()
        self.peer_datadir = self._setup_peer_datadir()

    # ── datadir setup ─────────────────────────────────────────────────────────

    def _setup_gui_datadir(self):
        datadir = os.path.join(self.tmpdir, "gui_node")
        os.makedirs(datadir, exist_ok=True)
        conf_path = os.path.join(datadir, "bitcoin.conf")
        with open(conf_path, "w", encoding="utf8") as f:
            f.write("regtest=1\n")
            f.write("[regtest]\n")
            f.write("server=1\n")
            f.write(f"rpcuser={GUI_RPC_USER}\n")
            f.write(f"rpcpassword={GUI_RPC_PASS}\n")
            f.write(f"rpcport={_rpc_port(0)}\n")
            f.write(f"port={_p2p_port(0)}\n")
            f.write("bind=127.0.0.1\n")
            f.write("listen=1\n")
            f.write(_COMMON_CONF)
        return datadir

    def _setup_peer_datadir(self):
        datadir = os.path.join(self.tmpdir, "peer_node")
        os.makedirs(datadir, exist_ok=True)
        conf_path = os.path.join(datadir, "bitcoin.conf")
        with open(conf_path, "w", encoding="utf8") as f:
            f.write("regtest=1\n")
            f.write("[regtest]\n")
            f.write("server=1\n")
            f.write(f"rpcuser={GUI_RPC_USER}\n")
            f.write(f"rpcpassword={GUI_RPC_PASS}\n")
            f.write(f"rpcport={_rpc_port(1)}\n")
            f.write(f"port={_p2p_port(1)}\n")
            f.write("listen=0\n")
            f.write(f"connect=127.0.0.1:{_p2p_port(0)}\n")
            f.write(_COMMON_CONF)
        return datadir

    # ── start / stop ──────────────────────────────────────────────────────────

    def start(self):
        """Start the GUI node, connect QmlDriver, then start the peer node."""
        env = dict(os.environ)
        env["QT_QPA_PLATFORM"] = "offscreen"

        gui_args = [
            self.gui_binary,
            f"-datadir={self.gui_datadir}",
            f"-test-automation={self.socket_path}",
            "-disablewallet",
            "-qml_onboarded=1",
            "-logtimemicros",
            "-debug",
            "-debugexclude=leveldb",
        ]
        print(f"Starting GUI node: {' '.join(gui_args)}")
        self.gui_process = subprocess.Popen(
            gui_args,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

        self.driver = QmlDriver(self.socket_path, timeout=GUI_STARTUP_TIMEOUT)
        print("QmlDriver connected to test bridge.")

        # Wait until the GUI node's P2P listener is ready to accept connections.
        _wait_for_port("127.0.0.1", _p2p_port(0))

        peer_args = [
            self.bitcoind_binary,
            f"-datadir={self.peer_datadir}",
        ]
        print(f"Starting peer node: {' '.join(peer_args)}")
        self.peer_process = subprocess.Popen(
            peer_args,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

    def stop(self):
        """Terminate all processes."""
        for proc in (self.peer2_process, self.peer_process, self.gui_process):
            if proc and proc.poll() is None:
                proc.send_signal(signal.SIGTERM)
                try:
                    proc.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait()
        if self.driver:
            self.driver.close()

    # ── RPC helpers ───────────────────────────────────────────────────────────

    def rpc_call(self, method, params=None):
        """Make a JSON-RPC call to the GUI node and return the result."""
        return self._rpc_call_to_port(_rpc_port(0), method, params)

    def _rpc_call_to_port(self, port, method, params=None):
        """Make a JSON-RPC call to a specific node RPC port."""
        payload = json.dumps({
            "jsonrpc": "1.0",
            "id": "qml_test",
            "method": method,
            "params": params or [],
        }).encode("utf-8")

        conn = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
        credentials = base64.b64encode(
            f"{GUI_RPC_USER}:{GUI_RPC_PASS}".encode("utf-8")
        ).decode("ascii")
        conn.request(
            "POST", "/",
            body=payload,
            headers={
                "Content-Type": "application/json",
                "Authorization": f"Basic {credentials}",
            },
        )
        resp = conn.getresponse()
        body = json.loads(resp.read())
        conn.close()
        if body.get("error"):
            raise RuntimeError(f"RPC error: {body['error']}")
        return body["result"]

    def peer_rpc_call(self, peer_idx, method, params=None):
        """Make a JSON-RPC call to a peer node.

        peer_idx:
          1 -> harness.peer_process
          2 -> harness.peer2_process
        """
        return self._rpc_call_to_port(_rpc_port(peer_idx), method, params)

    def wait_for_peer(self, timeout=30):
        """Poll getpeerinfo until at least one peer is connected."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                peers = self.rpc_call("getpeerinfo")
                if peers:
                    print(f"  Peer connected: id={peers[0]['id']} addr={peers[0]['addr']}")
                    return peers[0]["id"]
            except Exception:
                pass
            time.sleep(0.5)
        raise RuntimeError(f"No peer connected after {timeout}s")

    def wait_for_no_peers(self, timeout=PEER_ACTION_TIMEOUT_SECS):
        """Poll getpeerinfo until no peers remain."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                if not self.rpc_call("getpeerinfo"):
                    return
            except Exception:
                pass
            time.sleep(0.5)
        raise RuntimeError(f"Peer still connected after {timeout}s")

    def wait_for_no_banned(self, timeout=10):
        """Poll listbanned until the ban list is empty."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                if not self.rpc_call("listbanned"):
                    return
            except Exception:
                pass
            time.sleep(0.5)
        raise RuntimeError(f"Ban list not empty after {timeout}s")

    def wait_for_banned(self, min_entries=1, timeout=BAN_RPC_APPLY_TIMEOUT_SECS):
        """Poll listbanned until at least min_entries are present."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                banned = self.rpc_call("listbanned")
                if len(banned) >= min_entries:
                    return banned
            except Exception:
                pass
            time.sleep(0.5)
        raise RuntimeError(
            f"Expected at least {min_entries} banned peer(s) after {timeout}s"
        )

    def reconnect_peer(self):
        """Restart the peer bitcoind so it reconnects to the GUI node."""
        _terminate_process(self.peer_process)

        peer_args = [
            self.bitcoind_binary,
            f"-datadir={self.peer_datadir}",
        ]
        print("  Restarting peer node ...")
        self.peer_process = subprocess.Popen(
            peer_args,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        return self.wait_for_peer()

    def restart_gui(self):
        """Terminate the GUI process and start a fresh one with the same datadir.

        Used to verify that data (ban list, settings) persist across restarts.
        After this call, harness.driver is a new QmlDriver connected to the
        restarted process.
        """
        if self.driver:
            self.driver.close()
            self.driver = None
        _terminate_process(self.gui_process)

        # Remove the stale socket file so the new GUI can bind to the same path.
        if os.path.exists(self.socket_path):
            os.remove(self.socket_path)

        env = dict(os.environ)
        env["QT_QPA_PLATFORM"] = "offscreen"
        gui_args = [
            self.gui_binary,
            f"-datadir={self.gui_datadir}",
            f"-test-automation={self.socket_path}",
            "-disablewallet",
            "-qml_onboarded=1",
            "-logtimemicros",
            "-debug",
            "-debugexclude=leveldb",
        ]
        print(f"  Restarting GUI: {' '.join(gui_args)}")
        self.gui_process = subprocess.Popen(
            gui_args,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        self.driver = QmlDriver(self.socket_path, timeout=GUI_STARTUP_TIMEOUT)
        print("  QmlDriver reconnected after GUI restart.")

    def start_additional_peer(self):
        """Start a second peer bitcoind that connects to the GUI node."""
        _terminate_process(self.peer2_process)

        datadir = os.path.join(self.tmpdir, "peer2_node")
        os.makedirs(datadir, exist_ok=True)
        conf_path = os.path.join(datadir, "bitcoin.conf")
        with open(conf_path, "w", encoding="utf8") as f:
            f.write("regtest=1\n")
            f.write("[regtest]\n")
            f.write("server=1\n")
            f.write(f"rpcuser={GUI_RPC_USER}\n")
            f.write(f"rpcpassword={GUI_RPC_PASS}\n")
            f.write(f"rpcport={_rpc_port(2)}\n")
            f.write(f"port={_p2p_port(2)}\n")
            f.write("listen=0\n")
            f.write(f"connect=127.0.0.1:{_p2p_port(0)}\n")
            f.write(_COMMON_CONF)
        peer2_args = [self.bitcoind_binary, f"-datadir={datadir}"]
        print(f"  Starting second peer: {' '.join(peer2_args)}")
        self.peer2_process = subprocess.Popen(
            peer2_args,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

    def wait_for_n_peers(self, n, timeout=30):
        """Poll getpeerinfo until at least n peers are connected.

        Returns a list of node IDs as reported by getpeerinfo (sorted by peer id).
        """
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                peers = self.rpc_call("getpeerinfo")
                if len(peers) >= n:
                    ids = sorted(p["id"] for p in peers)
                    print(f"  {n} peer(s) connected: ids={ids}")
                    return ids
            except Exception:
                pass
            time.sleep(0.5)
        raise RuntimeError(f"Expected {n} peers after {timeout}s")

    def wait_for_exact_peer_ids(self, expected_ids, timeout=PEER_ACTION_TIMEOUT_SECS):
        """Poll getpeerinfo until the peer set matches expected_ids."""
        expected = sorted(expected_ids)
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                actual = sorted(p["id"] for p in self.rpc_call("getpeerinfo"))
                if actual == expected:
                    return
            except Exception:
                pass
            time.sleep(0.3)
        raise RuntimeError(
            f"Expected peer ids {expected} after {timeout}s, "
            f"got {sorted(p['id'] for p in self.rpc_call('getpeerinfo'))}"
        )

    def gui_peer_for_process(self, peer_idx):
        """Return the GUI-side getpeerinfo entry for a peer process via session_id."""
        peer_connections = self.peer_rpc_call(peer_idx, "getpeerinfo")
        if len(peer_connections) != 1:
            raise RuntimeError(
                f"Expected peer {peer_idx} to have exactly 1 connection, got: {peer_connections}"
            )

        session_id = peer_connections[0]["session_id"]
        for peer in self.rpc_call("getpeerinfo"):
            if peer.get("session_id") == session_id:
                return peer

        raise RuntimeError(
            f"Could not match peer {peer_idx} session {session_id} to GUI peer list"
        )


# ── Navigation helpers ────────────────────────────────────────────────────────

def navigate_to_peers(gui):
    """From the NodeRunner main screen, navigate to the Peers list page."""
    # Peers moved out of node settings into a dedicated NodeRunner header tab.
    gui.click("peersTabButton")
    gui.wait_for_page("peers")
    _wait_for_page_stack_idle(gui)


def _wait_for_page_stack_idle(gui, timeout_ms=PEER_ACTION_TIMEOUT_SECS * 1000) -> None:
    """Wait until page-stack transitions to/from the Peers page have settled.

    Peers live on the main page stack, so wait
    on the app's stack views via settle() (missing stacks are ignored)."""
    gui.settle(timeout_ms=timeout_ms)


def _open_peer_details(gui, node_id: int) -> None:
    _wait_for_page_stack_idle(gui)
    gui.click(f"peerListItem_{node_id}")
    gui.wait_for_page("peerDetails", timeout_ms=8000)
    _wait_for_page_stack_idle(gui)
    print(f"  Opened PeerDetails for node id={node_id}")


# ── Individual test cases ─────────────────────────────────────────────────────

def test_disconnect_peer(gui, harness, node_id):
    print("\n── test_disconnect_peer ──────────────────────────────────────────")

    _open_peer_details(gui, node_id)

    gui.click("peerDisconnectButton")
    print("  Clicked Disconnect")

    # Stop the peer process so it cannot reconnect before we poll.
    # (The peer is configured with connect=, so it retries immediately.)
    _terminate_process(harness.peer_process)

    harness.wait_for_no_peers()
    peers = harness.rpc_call("getpeerinfo")
    assert peers == [], f"Expected no peers after disconnect, got: {peers}"
    print("  PASSED: peer is disconnected")
    # PeerDetails.qml automatically calls root.back() on the onDisconnected
    # signal, so no manual navigation is needed here.


def test_ban_peer(gui, harness, node_id, duration_secs, duration_label):
    print(f"\n── test_ban_peer ({duration_label}) ──────────────────────────────")

    _open_peer_details(gui, node_id)

    gui.click("peerBanButton")
    gui.wait_for_property(f"banDurationRow_{duration_secs}", "visible", True)

    gui.click(f"banDurationRow_{duration_secs}")

    gui.click("banConfirmButton")
    print(f"  Confirmed ban ({duration_label})")

    # Verify ban was recorded before stopping the peer.
    harness.wait_for_banned(min_entries=1)

    # Kill peer to stop reconnection; peer uses connect=, retries immediately.
    _terminate_process(harness.peer_process)

    harness.wait_for_no_peers()

    peers = harness.rpc_call("getpeerinfo")
    assert peers == [], f"Expected no peers after ban, got: {peers}"

    banned = harness.rpc_call("listbanned")
    assert len(banned) == 1, f"Expected 1 banned entry, got: {banned}"
    entry = banned[0]
    assert abs(entry["ban_duration"] - duration_secs) < 10, \
        f"Expected ban duration ~{duration_secs}s, got {entry['ban_duration']}s"
    print(f"  PASSED: peer banned ({entry['address']}) for ~{duration_label}")

    # Wait for PeerDetails to navigate back via its onDisconnected handler.
    gui.wait_for_page("peers", timeout_ms=PEER_ACTION_TIMEOUT_SECS * 1000)
    _wait_for_page_stack_idle(gui)


def test_unban_peer(gui, harness):
    print("\n── test_unban_peer ───────────────────────────────────────────────")

    # StackView disables input during transitions (500ms pop animation for
    # PeerDetails→Peers). Wait for it to finish; pushing BannedPeers while
    # the StackView is busy is silently ignored by Qt.
    _wait_for_page_stack_idle(gui)
    gui.wait_for_property("viewBannedPeersButton", "enabled", True,
                          timeout_ms=PEER_ACTION_TIMEOUT_SECS * 1000)

    # The ban list button is in the Peers page footer.
    gui.click("viewBannedPeersButton")
    gui.wait_for_page("bannedPeers", timeout_ms=8000)
    _wait_for_page_stack_idle(gui)
    print("  Navigated to BannedPeers page")

    gui.click("unbanButton_0")
    harness.wait_for_no_banned()

    banned = harness.rpc_call("listbanned")
    assert banned == [], f"Expected empty ban list after unban, got: {banned}"
    print("  PASSED: ban list is empty")


def test_ban_persistence(harness):
    """Ban a peer, restart the GUI node, verify the ban survives the restart."""
    gui = harness.driver
    print("\n── test_ban_persistence ──────────────────────────────────────")

    node_id = harness.reconnect_peer()
    gui.wait_for_property(f"peerListItem_{node_id}", "visible", True, timeout_ms=PEER_LIST_ITEM_VISIBLE_TIMEOUT_MS)
    test_ban_peer(gui, harness, node_id, 3600, "1 hour")

    print("  Restarting GUI node ...")
    harness.restart_gui()

    navigate_to_peers(harness.driver)
    harness.driver.wait_for_property(
        "viewBannedPeersButton",
        "visible",
        True,
        timeout_ms=PEER_ACTION_TIMEOUT_SECS * 1000,
    )

    assert harness.driver.get_property("viewBannedPeersButton", "visible"), \
        "viewBannedPeersButton should be visible after GUI restart (ban persisted)"

    banned = harness.rpc_call("listbanned")
    assert len(banned) == 1, f"Expected 1 banned entry after restart, got: {banned}"
    print(f"  PASSED: ban for {banned[0]['address']} persisted across GUI restart")

    harness.rpc_call("clearbanned")


def _wait_for_peer_gone(harness, node_id, timeout=PEER_ACTION_TIMEOUT_SECS):
    """Poll until node_id is no longer in getpeerinfo."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            peers = harness.rpc_call("getpeerinfo")
            if not any(p["id"] == node_id for p in peers):
                return
        except Exception:
            pass
        time.sleep(0.3)
    raise RuntimeError(f"Peer {node_id} did not disconnect in time")


def test_disconnect_specific_peer(gui, harness):
    """With 2 peers connected, disconnect one and verify the other stays."""
    print("\n── test_disconnect_specific_peer ────────────────────────────")

    harness.start_additional_peer()
    harness.wait_for_n_peers(2)

    # Use session_id to map the GUI-side node id back to a specific peer
    # process. Relying on getpeerinfo ordering is brittle and can disconnect
    # the correct peer while terminating the wrong process.
    target_peer = harness.gui_peer_for_process(1)
    other_peer = harness.gui_peer_for_process(2)
    target_id = target_peer["id"]
    other_id = other_peer["id"]

    gui.wait_for_property(f"peerListItem_{target_id}", "visible", True, timeout_ms=PEER_LIST_ITEM_VISIBLE_TIMEOUT_MS)

    _open_peer_details(gui, target_id)
    gui.click("peerDisconnectButton")
    print(f"  Clicked Disconnect for peer {target_id}")

    # Stop peer_process so it cannot reconnect before we poll.
    # (peer_process is configured with connect=, so it retries immediately.)
    _terminate_process(harness.peer_process)

    _wait_for_peer_gone(harness, target_id)
    harness.wait_for_exact_peer_ids([other_id])

    peers = harness.rpc_call("getpeerinfo")
    remaining_ids = [p["id"] for p in peers]
    assert target_id not in remaining_ids, \
        f"Peer {target_id} should be disconnected"
    assert len(peers) == 1, \
        f"Expected exactly 1 peer remaining, got {len(peers)}: {remaining_ids}"
    assert other_id in remaining_ids, \
        f"Peer {other_id} should still be connected"
    print(f"  PASSED: peer {target_id} gone; peer {other_id} still present")

    # Leave the harness in a clean state for subsequent tests and future runs.
    _terminate_process(harness.peer2_process)
    harness.wait_for_no_peers()


def test_ban_one_of_two_peers(gui, harness):
    """Ban one peer and verify the other is also disconnected due to subnet ban.

    Bitcoin Core bans by /32 subnet for IPv4 loopback addresses. Since both
    peer nodes connect from 127.0.0.1, banning one causes the node to reject
    and disconnect all connections from that subnet — including the second peer.
    """
    print("\n── test_ban_one_of_two_peers ─────────────────────────────────")

    harness.start_additional_peer()
    peer_ids = harness.wait_for_n_peers(2)
    target_id = peer_ids[0]

    gui.wait_for_property(f"peerListItem_{target_id}", "visible", True, timeout_ms=PEER_LIST_ITEM_VISIBLE_TIMEOUT_MS)

    _open_peer_details(gui, target_id)
    gui.click("peerBanButton")
    gui.wait_for_property("banDurationRow_3600", "visible", True)
    gui.click("banDurationRow_3600")
    gui.click("banConfirmButton")
    print(f"  Banned peer {target_id} (subnet: 127.0.0.1/32)")

    # Verify ban was recorded before stopping the peers.
    harness.wait_for_banned(min_entries=1)

    # Kill both peer processes — both share 127.0.0.1 and retry immediately.
    for proc_attr in ("peer_process", "peer2_process"):
        _terminate_process(getattr(harness, proc_attr, None))

    harness.wait_for_no_peers()

    peers = harness.rpc_call("getpeerinfo")
    assert peers == [], \
        f"Expected no peers after banning 127.0.0.1/32 (subnet ban), got: {peers}"

    banned = harness.rpc_call("listbanned")
    assert len(banned) == 1, f"Expected 1 banned entry, got: {banned}"
    print(f"  PASSED: banning peer {target_id} disconnected all peers from "
          f"{banned[0]['address']} (subnet ban)")

    harness.rpc_call("clearbanned")


# ── Entry point ───────────────────────────────────────────────────────────────

def run_tests():
    harness = PeerQmlTestHarness()
    try:
        harness.start()
        gui = harness.driver

        # ── Wait for peer to connect ─────────────────────────────────────────
        print("\nWaiting for peer to connect ...")
        node_id = harness.wait_for_peer()

        # ── Navigate to Peers page ───────────────────────────────────────────
        print("\nNavigating to Peers page ...")
        navigate_to_peers(gui)
        gui.wait_for_property(f"peerListItem_{node_id}", "visible", True, timeout_ms=PEER_LIST_ITEM_VISIBLE_TIMEOUT_MS)

        assert not gui.get_property("viewBannedPeersButton", "visible"), \
            "viewBannedPeersButton should be hidden when ban list is empty"
        print("  Verified: viewBannedPeersButton hidden (no bans)")

        # ── Test 1: Disconnect ───────────────────────────────────────────────
        test_disconnect_peer(gui, harness, node_id)

        assert not gui.get_property("viewBannedPeersButton", "visible"), \
            "viewBannedPeersButton should still be hidden after disconnect"
        print("  Verified: viewBannedPeersButton hidden (no bans after disconnect)")

        # ── Tests 2–5: Ban with each duration ────────────────────────────────
        for i, (duration_secs, duration_label) in enumerate(BAN_DURATIONS):
            if i > 0:
                harness.rpc_call("clearbanned")
            print(f"\nReconnecting peer for ban test ({duration_label}) ...")
            node_id = harness.reconnect_peer()
            gui.wait_for_property(f"peerListItem_{node_id}", "visible", True, timeout_ms=PEER_LIST_ITEM_VISIBLE_TIMEOUT_MS)

            assert not gui.get_property("viewBannedPeersButton", "visible"), \
                f"viewBannedPeersButton should be hidden before ban ({duration_label})"
            print("  Verified: viewBannedPeersButton hidden (no active ban)")

            test_ban_peer(gui, harness, node_id, duration_secs, duration_label)

            gui.wait_for_property("viewBannedPeersButton", "visible", True)

            assert gui.get_property("viewBannedPeersButton", "visible"), \
                f"viewBannedPeersButton should be visible after ban ({duration_label})"
            print("  Verified: viewBannedPeersButton visible (peer banned)")

        # ── Test 6: Unban ────────────────────────────────────────────────────
        # The 1-year ban from the last iteration is still in the ban list.
        test_unban_peer(gui, harness)

        _wait_for_page_stack_idle(gui)
        gui.click("bannedPeersBackButton")
        gui.wait_for_page("peers")
        _wait_for_page_stack_idle(gui)

        assert not gui.get_property("viewBannedPeersButton", "visible"), \
            "viewBannedPeersButton should be hidden after UI unban"
        print("  Verified: viewBannedPeersButton hidden (ban cleared via UI)")

        # ── Test 7: Ban persistence across GUI restart ────────────────────
        print("\nRunning ban persistence test ...")
        test_ban_persistence(harness)
        gui = harness.driver

        # ── Tests 8–9: Multi-peer isolation ──────────────────────────────
        # test_ban_persistence already navigated to the peers page after
        # restarting the GUI, so no additional navigation is needed here.
        print("\nReconnecting peer for multi-peer disconnect test ...")
        peer1_id = harness.reconnect_peer()
        gui.wait_for_property(f"peerListItem_{peer1_id}", "visible", True, timeout_ms=PEER_LIST_ITEM_VISIBLE_TIMEOUT_MS)
        test_disconnect_specific_peer(gui, harness)

        print("\nReconnecting peer for multi-peer ban test ...")
        peer1_id = harness.reconnect_peer()
        navigate_to_peers(gui)
        gui.wait_for_property(f"peerListItem_{peer1_id}", "visible", True, timeout_ms=PEER_LIST_ITEM_VISIBLE_TIMEOUT_MS)
        test_ban_one_of_two_peers(gui, harness)

        print("\n" + "=" * 60)
        print("All peer management tests PASSED")
        print("=" * 60)

    except Exception as e:
        print(f"\nFAILED: {e}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        if harness.gui_process:
            try:
                harness.gui_process.send_signal(signal.SIGTERM)
                try:
                    stderr_bytes = harness.gui_process.communicate(timeout=5)[1]
                except subprocess.TimeoutExpired:
                    harness.gui_process.kill()
                    stderr_bytes = harness.gui_process.communicate()[1]
                if stderr_bytes:
                    print("\n--- GUI node stderr ---", file=sys.stderr)
                    print(stderr_bytes.decode("utf-8", errors="replace")[-4000:], file=sys.stderr)
            except Exception:
                pass
        if harness.driver:
            dump_qml_tree(harness.driver)
        sys.exit(1)
    finally:
        harness.stop()


if __name__ == '__main__':
    run_tests()
