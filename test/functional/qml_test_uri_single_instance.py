#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Functional test for payment URIs handed to an already running instance.

Requires:
  - bitcoin-core-app built with -DENABLE_TEST_AUTOMATION=ON
  - bitcoind built with -DBUILD_DAEMON=ON
"""

import fcntl
import hashlib
import os
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time

from qml_test_harness import qml_qpa_platform, qsettings_sandbox_args
from qml_wallet_test_lib import (
    WalletFlowHarness,
    pick_unused_port,
    rpc_call,
    wait_for_rpc,
    write_datadir,
)

WALLET_NAME = "testwallet"
REVIEW_POPUP = "sendPaymentRequestReviewPopup"
HANDOFF_TIMEOUT = 30


def server_path(datadir):
    """Mirror PaymentUriIpc::ServerName for the regtest data directory."""
    network_dir = os.path.realpath(os.path.join(datadir, "regtest"))
    digest = hashlib.sha256(network_dir.encode("utf-8")).hexdigest()[:16]
    return os.path.join(tempfile.gettempdir(), f"BitcoinCoreApp-{digest}")


def wait_for_wallet(gui):
    gui.wait_for_property("walletBadge", "loading", False, timeout_ms=30000)
    gui.wait_for_property("walletBadge", "text", WALLET_NAME, timeout_ms=30000)
    gui.wait_for_property("walletBadge", "noWalletLoaded", False, timeout_ms=10000)


def wait_for_review(gui):
    gui.wait_for_property(REVIEW_POPUP, "opened", True, timeout_ms=20000)


def review_address(gui):
    return str(gui.get_property("paymentRequestReviewAddress", "text"))


class SecondInstance:
    def __init__(self, harness, uris, datadir=None):
        self.tmpdir = tempfile.mkdtemp(prefix="qml_test_uri_second_")
        self.bridge_path = os.path.join(self.tmpdir, "bridge.sock")
        self.output_path = os.path.join(self.tmpdir, "output.txt")
        env = dict(os.environ)
        env["QT_QPA_PLATFORM"] = qml_qpa_platform()
        args = [
            harness.gui_binary,
            f"-datadir={datadir or harness.gui_datadir}",
            f"-test-automation={self.bridge_path}",
            *qsettings_sandbox_args(env, harness.config_home),
            "-qml_onboarded=1",
            "-nolisten",
            *uris,
        ]
        with open(self.output_path, "wb") as output_file:
            self.process = subprocess.Popen(args, env=env, stdout=output_file, stderr=subprocess.STDOUT)

    def output(self):
        with open(self.output_path, "rb") as output_file:
            return output_file.read().decode("utf-8", errors="replace")

    def wait_for_exit(self, timeout=HANDOFF_TIMEOUT):
        try:
            return self.process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            raise AssertionError(f"The second instance did not exit\n{self.output()}")

    def wait_for_output(self, needle, timeout=HANDOFF_TIMEOUT):
        deadline = time.monotonic() + timeout
        while needle not in self.output():
            if time.monotonic() >= deadline:
                raise AssertionError(f"Expected output not found: {needle}\n{self.output()}")
            time.sleep(0.1)

    def started_gui(self):
        return os.path.exists(self.bridge_path)

    def terminate(self):
        if self.process.poll() is not None:
            return
        self.process.send_signal(signal.SIGTERM)
        try:
            self.process.wait(timeout=30)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()


def hand_over(harness, uris):
    second = SecondInstance(harness, uris)
    try:
        code = second.wait_for_exit()
        assert code == 0, f"The second instance exited with {code}\n{second.output()}"
        assert not second.started_gui(), "The second instance built its own GUI"
    finally:
        second.terminate()


class FakeReceiver:
    POLL = 0.2

    def __init__(self, path, delay=0.0):
        self.path = path
        self.delay = delay
        self.stop = threading.Event()
        self.sock = None
        # The sender may start right away.
        if not delay:
            self._bind()
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()

    def _bind(self):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.settimeout(self.POLL)
        self.sock.bind(self.path)
        self.sock.listen(1)

    def _run(self):
        if self.delay:
            if self.stop.wait(self.delay):
                return
            self._bind()
        while not self.stop.is_set():
            try:
                conn, _ = self.sock.accept()
            except socket.timeout:
                continue
            except OSError:
                return
            with conn:
                conn.settimeout(HANDOFF_TIMEOUT)
                try:
                    self.handle(conn)
                except OSError:
                    pass

    def handle(self, conn):
        raise NotImplementedError

    def close(self):
        self.stop.set()
        self.thread.join(timeout=HANDOFF_TIMEOUT)
        assert not self.thread.is_alive(), "The fake receiver thread did not stop"
        if self.sock:
            self.sock.close()
        if os.path.exists(self.path):
            os.unlink(self.path)


class DelayedReceiver(FakeReceiver):
    def __init__(self, path, lock_path, delay):
        self.lock_file = open(lock_path, "a")
        fcntl.lockf(self.lock_file, fcntl.LOCK_EX | fcntl.LOCK_NB)
        self.received = b""
        super().__init__(path, delay)

    def handle(self, conn):
        self.received = conn.recv(65536)
        conn.sendall(b"\x00")

    def close(self):
        try:
            super().close()
        finally:
            self.lock_file.close()


class AcceptAndClose(FakeReceiver):
    def __init__(self, path):
        self.connections = 0
        super().__init__(path)

    def handle(self, conn):
        self.connections += 1
        conn.recv(1024)


def run_tests():
    harness = WalletFlowHarness("qml_test_uri_single_instance", port_offset=520)
    try:
        harness.start_gui()
        wait_for_rpc(harness.gui_rpc_port)
        gui = harness.driver
        gui.wait_for_property("walletBadge", "loading", False, timeout_ms=20000)
        rpc_call(harness.gui_rpc_port, "createwallet", {"wallet_name": WALLET_NAME, "load_on_startup": True})
        wait_for_wallet(gui)
        first_address = rpc_call(harness.gui_rpc_port, "getnewaddress", ["first", "bech32"], wallet=WALLET_NAME)
        second_address = rpc_call(harness.gui_rpc_port, "getnewaddress", ["second", "bech32"], wallet=WALLET_NAME)

        hand_over(harness, [f"bitcoin:{first_address}?amount=0.0123&label=handed-over&message=a=b"])
        gui.wait_for_page("sendPage", timeout_ms=20000)
        wait_for_review(gui)
        assert first_address in review_address(gui)
        assert "a=b" in str(gui.get_property("paymentRequestReviewMessage", "text"))
        gui.click("paymentRequestReviewApplyButton")
        gui.wait_for_property(
            "sendPaymentRequestPayToValue", "text",
            lambda v: "handed-over" in str(v), timeout_ms=20000,
        )
        print("Test 1 PASSED: URI handed over and applied in the running instance.")

        gui.wait_for_property(REVIEW_POPUP, "opened", False, timeout_ms=10000)
        hand_over(harness, [
            f"bitcoin:{second_address}?label=batch-one",
            f"bitcoin:{first_address}?label=batch-two",
        ])
        wait_for_review(gui)
        assert second_address in review_address(gui)
        gui.click("paymentRequestReviewApplyButton")
        gui.wait_for_property(
            "sendPaymentRequestPayToValue", "text",
            lambda v: "batch-one" in str(v), timeout_ms=20000,
        )
        gui.wait_for_property(REVIEW_POPUP, "replacesValues", True, timeout_ms=20000)
        assert first_address in review_address(gui)
        gui.click("paymentRequestReviewDiscardButton")
        gui.wait_for_property(REVIEW_POPUP, "opened", False, timeout_ms=10000)
        assert "batch-one" in str(gui.get_property("sendPaymentRequestPayToValue", "text"))
        print("Test 2 PASSED: a batch is delivered in order, one review at a time.")

        gui.set_property("appWindow", "visible", False)
        gui.wait_for_property("appWindow", "visible", False, timeout_ms=5000)
        hand_over(harness, [f"bitcoin:{first_address}?label=from-hidden"])
        gui.wait_for_property("appWindow", "visible", True, timeout_ms=10000)
        wait_for_review(gui)
        gui.click("paymentRequestReviewDiscardButton")
        gui.wait_for_property(REVIEW_POPUP, "opened", False, timeout_ms=10000)
        print("Test 3 PASSED: hidden window shown for the request.")

        other_datadir = os.path.join(harness.tmpdir, "other_node")
        write_datadir(other_datadir, pick_unused_port(), pick_unused_port())
        other = SecondInstance(harness, [f"bitcoin:{first_address}?label=elsewhere"], datadir=other_datadir)
        try:
            deadline = time.monotonic() + HANDOFF_TIMEOUT
            while not other.started_gui():
                assert other.process.poll() is None, f"The other instance exited\n{other.output()}"
                assert time.monotonic() < deadline, f"The other instance did not start\n{other.output()}"
                time.sleep(0.2)
            assert gui.get_property(REVIEW_POPUP, "opened") is False
        finally:
            other.terminate()
        print("Test 4 PASSED: another data directory starts its own instance.")

        harness.stop_gui()
        path = server_path(harness.gui_datadir)
        assert not os.path.exists(path), "The receiver socket outlived a clean shutdown"
        fake = AcceptAndClose(path)
        second = SecondInstance(harness, [f"bitcoin:{first_address}"])
        try:
            second.wait_for_output("could not be handed to the running instance")
            assert fake.connections == 1, f"Expected one attempt, got {fake.connections}"
            assert not second.started_gui()
        finally:
            second.terminate()
            fake.close()
        print("Test 5 PASSED: an unconfirmed hand-off is reported and not retried.")

        harness.start_gui()
        harness.gui_process.kill()
        harness.gui_process.wait()
        assert os.path.exists(path), "Expected the crashed instance to leave its socket behind"
        harness.start_gui()
        hand_over(harness, [f"bitcoin:{second_address}?label=after-crash"])
        wait_for_rpc(harness.gui_rpc_port)
        gui = harness.driver
        wait_for_wallet(gui)
        wait_for_review(gui)
        assert second_address in review_address(gui)
        print("Test 6 PASSED: stale socket replaced, early request kept until the wallet loads.")

        harness.stop_gui()
        owner = DelayedReceiver(path, os.path.join(harness.gui_datadir, "regtest", ".lock"), delay=1.0)
        try:
            hand_over(harness, [f"bitcoin:{first_address}?label=late-owner"])
            assert b"late-owner" in owner.received, f"Unexpected request: {owner.received!r}"
        finally:
            owner.close()
        print("Test 7 PASSED: URIs reach an owner that started listening late.")

        print("\nAll single instance URI tests passed.")
    finally:
        harness.stop()


if __name__ == "__main__":
    try:
        run_tests()
    except Exception as e:
        print(f"FAILED: {e}", file=sys.stderr)
        sys.exit(1)
