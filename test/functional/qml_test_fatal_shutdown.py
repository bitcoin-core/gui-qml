#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Acknowledge fatal exceptions with a real initialized node and HTTP workers."""

from pathlib import Path
import re
import unittest

from qml_driver import QmlDriverError
from qml_process_checks import report_gui_failure, unexpected_gui_stderr
from qml_wallet_test_lib import WalletFlowHarness, rpc_call, wait_for_rpc


class FatalShutdownTests(unittest.TestCase):
    def test_initialized_node_fatal_exit(self):
        for phase in ("initialize", "interrupt", "shutdown"):
            for acknowledgement in ("button", "quit", "close"):
                with self.subTest(phase=phase, acknowledgement=acknowledgement):
                    self.check_fatal_exit(phase, acknowledgement)

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
    unittest.main(verbosity=2)
