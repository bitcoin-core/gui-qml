#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import contextlib
import io
import subprocess
import sys
import unittest
from unittest import mock

from qml_process_checks import check_gui_exit, report_gui_failure, unexpected_gui_stderr


class GuiProcessChecksTest(unittest.TestCase):
    def test_known_qt_messages_after_stripping(self):
        messages = (
            "This plugin does not support application fonts",
            'Failed to load font resource: ":/fonts/bitcoincoresans/regular"',
            'Failed to load font resource: ":/fonts/bitcoincoresans/semibold"',
            'Failed to load font resource: ":/fonts/robotomono/regular"',
            "QObject::connect: No such signal QPlatformNativeInterface::systemTrayWindowChanged(QScreen*)",
            "qt.core.qobject.connect: QObject::connect: No such signal QPlatformNativeInterface::systemTrayWindowChanged(QScreen*)",
            "Running initialization in thread",
        )
        for message in messages:
            for source in ("", " (qml/bitcoin.cpp:123)"):
                for final_newline in ("", "\n"):
                    with self.subTest(message=message, source=source, final_newline=final_newline):
                        stderr = ("\n  " + message + source + "  " + final_newline).strip()
                        check_gui_exit(0, stderr)

    def test_unexpected_diagnostics_fail(self):
        for message in (
            "WARNING: ThreadSanitizer: data race",
            "ERROR: AddressSanitizer: heap-use-after-free",
            "ERROR: LeakSanitizer: detected memory leaks",
            "runtime error: signed integer overflow",
            "QThread: Destroyed while thread is still running",
            "QObject::connect: No such signal Missing::other()",
            "Running initialization in thread: unexpected warning",
            "This plugin does not support application fonts\nUnexpected warning",
        ):
            with self.subTest(message=message):
                self.assertTrue(unexpected_gui_stderr(message))
                with self.assertRaises(AssertionError):
                    check_gui_exit(0, message)

    def test_bridge_messages_match_the_expected_socket(self):
        path = "/tmp/qml_test_123/test_bridge.sock"
        stderr = f"TestBridge: listening on {path}\nTestBridge: client connected\nTestBridge: client disconnected"
        check_gui_exit(0, stderr, socket_path=path)
        for invalid in (stderr.replace(path, "/tmp/other.sock"), stderr + "\nERROR: AddressSanitizer: memory error"):
            with self.assertRaises(AssertionError):
                check_gui_exit(0, invalid, socket_path=path)
        with self.assertRaises(AssertionError):
            check_gui_exit(0, stderr)

    def test_unsuccessful_exit_fails_even_without_stderr(self):
        for returncode in (1, -11, None):
            with self.subTest(returncode=returncode):
                with self.assertRaises(AssertionError):
                    check_gui_exit(returncode, "")

    def test_failure_drains_stderr_beyond_pipe_capacity_before_exit(self):
        diagnostic_larger_than_pipe_capacity = "WARNING: ThreadSanitizer: data race\n" + "trace detail\n" * 20000
        script = "import sys; sys.stderr.write(sys.stdin.read()); sys.exit(66)"
        with subprocess.Popen([sys.executable, "-c", script], stdin=subprocess.PIPE,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE) as process:
            process.stdin.write(diagnostic_larger_than_pipe_capacity.encode())
            process.stdin.close()
            process.stdin = None
            output = io.StringIO()
            with contextlib.redirect_stderr(output):
                report_gui_failure(process, "driver disconnect")
            self.assertEqual(process.returncode, 66)
            self.assertIn("exit status 66", output.getvalue())
            self.assertIn(diagnostic_larger_than_pipe_capacity, output.getvalue())

    def test_failure_terminates_unresponsive_process(self):
        script = "import sys, time; print('ready', flush=True); time.sleep(60)"
        with subprocess.Popen([sys.executable, "-c", script], stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE) as process:
            self.assertEqual(process.stdout.readline(), b"ready\n")
            with contextlib.redirect_stderr(io.StringIO()):
                report_gui_failure(process, "driver timeout", timeout=0.1)
            self.assertIsNotNone(process.returncode)
            self.assertNotEqual(process.returncode, 0)

    def test_diagnostic_error_does_not_replace_original_failure(self):
        process = mock.Mock()
        process.communicate.side_effect = OSError("closed pipe")
        original = ConnectionResetError("driver disconnected")
        with self.assertRaises(ConnectionResetError) as failure:
            try:
                raise original
            except BaseException:
                with contextlib.redirect_stderr(io.StringIO()) as output:
                    report_gui_failure(process, "driver disconnect")
                raise
        self.assertIs(failure.exception, original)
        self.assertIn("closed pipe", output.getvalue())


if __name__ == "__main__":
    unittest.main()
