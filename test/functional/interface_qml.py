#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the QML bitcoin-qt interface and automation bridge."""

import os
from pathlib import Path
import subprocess

from test_framework.qml_driver import QmlDriverError
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


class QmlInterfaceTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 0
        self.setup_clean_chain = True

    def setup_network(self):
        pass

    def skip_test_if_missing_module(self):
        self.skip_if_no_qml()

    def assert_non_regtest_rejected(self):
        test_dir = Path(self.options.tmpdir) / "automation_non_regtest"
        datadir = test_dir / "node"
        datadir.mkdir(parents=True)
        command = self.get_binaries().qml_argv() + [
            f"-datadir={datadir}",
            f"-test-automation={datadir / 'test_bridge.sock'}",
            "-printtoconsole=1",
        ]
        environment = dict(os.environ)
        environment["QT_QPA_PLATFORM"] = os.getenv("QML_TEST_QPA_PLATFORM", "minimal")
        result = subprocess.run(
            command,
            env=environment,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=10,
            check=False,
        )
        assert result.returncode != 0
        assert "The -test-automation option is only available on regtest." in result.stderr

    def assert_invalid_socket_rejected(self):
        test_dir = Path(self.options.tmpdir) / "automation_invalid_socket"
        datadir = test_dir / "node"
        datadir.mkdir(parents=True)
        socket_path = test_dir / "missing" / "test_bridge.sock"
        command = self.get_binaries().qml_argv() + [
            "-regtest",
            f"-datadir={datadir}",
            f"-test-automation={socket_path}",
            "-printtoconsole=1",
        ]
        environment = dict(os.environ)
        environment["QT_QPA_PLATFORM"] = os.getenv("QML_TEST_QPA_PLATFORM", "minimal")
        result = subprocess.run(
            command,
            env=environment,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=10,
            check=False,
        )
        assert result.returncode != 0
        assert "TestBridge: failed to listen" in result.stderr

    def run_test(self):
        self.log.info("Checking that test automation is rejected outside regtest")
        self.assert_non_regtest_rejected()
        if os.name != "nt":
            self.log.info("Checking that an invalid socket path is rejected")
            self.assert_invalid_socket_rejected()

        harness = None
        try:
            self.log.info("Starting the QML bitcoin-qt and connecting the test bridge")
            harness = self.start_qml()
            gui = harness.driver

            objects = gui.list_objects()
            object_names = {entry["objectName"] for entry in objects}
            assert "mainWindow" in object_names

            self.log.info("Checking the top-level window")
            assert_equal(gui.get_property("mainWindow", "visible"), True)
            assert_equal(gui.get_property("mainWindow", "title"), "Bitcoin Core")

            self.log.info("Checking bridge error handling")
            try:
                gui.get_property("missingObject", "visible")
            except QmlDriverError as error:
                assert "Object not found" in str(error)
            else:
                raise AssertionError("Missing QML object did not return a bridge error")

            self.log.info("Closing the application window through the bridge")
            gui.close_window()
            assert_equal(harness.wait_for_exit(), 0)
        except Exception:
            output = harness.process_output() if harness is not None else ""
            if output:
                self.log.error("QML bitcoin-qt output:\n%s", output)
            raise
        finally:
            if harness is not None:
                self.stop_qml(harness)


if __name__ == "__main__":
    QmlInterfaceTest(__file__).main()
