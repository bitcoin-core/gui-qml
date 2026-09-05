#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the QML bitcoin-qt interface and automation bridge."""

import os
from pathlib import Path
import subprocess

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

    def assert_non_regtest_rejected(self, *, name, multiprocess):
        test_dir = Path(self.options.tmpdir) / f"automation_non_regtest_{name}"
        datadir = test_dir / "node"
        datadir.mkdir(parents=True)
        command = self.get_binaries().qml_argv(multiprocess=multiprocess) + [
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

    def assert_invalid_socket_rejected(self, *, name, multiprocess):
        test_dir = Path(self.options.tmpdir) / f"automation_invalid_socket_{name}"
        datadir = test_dir / "node"
        datadir.mkdir(parents=True)
        socket_path = test_dir / "missing" / "test_bridge.sock"
        command = self.get_binaries().qml_argv(multiprocess=multiprocess) + [
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

    def test_gui(self, *, name, multiprocess):
        harness = None
        try:
            self.log.info("Starting QML %s and connecting the test bridge", name)
            harness = self.start_qml(
                multiprocess=multiprocess,
                extra_args=["-qml_onboarded=1"],
            )
            gui = harness.driver

            objects = gui.list_objects()
            object_names = {entry["objectName"] for entry in objects}
            for object_name in (
                "mainWindow",
                "nodeRunner",
                "blockClock",
                "nodeSettingsButton",
            ):
                assert object_name in object_names, (
                    f"missing {object_name!r}; found {sorted(object_names)}"
                )

            self.log.info("Checking the node runner window")
            assert_equal(gui.get_property("mainWindow", "visible"), True)
            assert_equal(gui.get_property("mainWindow", "title"), "Bitcoin Core")
            self.log.info("Waiting for the node to finish starting")
            self.wait_until(lambda: gui.get_property("mainWindow", "nodeStatus") == "Node is running", timeout=30)

            self.log.info("Opening display settings from the block clock")
            gui.click("nodeSettingsButton")
            self.wait_until(lambda: any(item["objectName"] == "gotoBlockClockSize" for item in gui.list_objects()))
            gui.click("nodeSettingsDoneButton")
            self.wait_until(lambda: any(item["objectName"] == "blockClock" for item in gui.list_objects()))

            self.log.info("Opening the console and executing a read-only node command")
            gui.click("consoleTabButton")
            self.wait_until(lambda: any(item["objectName"] == "consoleInput" for item in gui.list_objects()))
            gui.type_text("consoleInput", "getblockchaininfo")
            gui.press_key("consoleInput", "Escape")  # Dismiss completion before submitting.
            gui.press_key("consoleInput", "Return")

            def has_regtest_reply():
                for item in gui.list_objects():
                    if item["objectName"].startswith("consoleOutputArea_content_"):
                        if "regtest" in gui.get_property(item["objectName"], "text"):
                            return True
                return False

            self.wait_until(has_regtest_reply, timeout=30)

            self.log.info("Closing the application window through the bridge")
            gui.close_window()
            assert_equal(harness.wait_for_exit(), 0)
        except Exception:
            output = harness.process_output() if harness is not None else ""
            if output:
                self.log.error("QML %s output:\n%s", name, output)
            raise
        finally:
            if harness is not None:
                self.stop_qml(harness)

    def run_test(self):
        variants = [("bitcoin-qt", False)]
        if self.is_ipc_compiled():
            variants.append(("bitcoin-gui", True))

        for name, multiprocess in variants:
            self.log.info("Checking that %s test automation is rejected outside regtest", name)
            self.assert_non_regtest_rejected(name=name, multiprocess=multiprocess)
            if os.name != "nt":
                self.log.info("Checking that %s rejects an invalid socket path", name)
                self.assert_invalid_socket_rejected(name=name, multiprocess=multiprocess)
            self.test_gui(name=name, multiprocess=multiprocess)


if __name__ == "__main__":
    QmlInterfaceTest(__file__).main()
