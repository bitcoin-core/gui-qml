#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import unittest

from gui_process_check import unexpected_stderr


class StderrCheckTests(unittest.TestCase):
    def test_known_qt_variants(self):
        warning = "QObject::connect: No such signal QPlatformNativeInterface::systemTrayWindowChanged(QScreen*)"
        for prefix in ["", "qt.core.qobject.connect: "]:
            for suffix in ["", "\n", " (qobject.cpp:5321)", " (qobject.cpp:5321)\n"]:
                self.assertEqual(unexpected_stderr(prefix + warning + suffix), [])

    def test_unexpected_output_is_not_hidden(self):
        for warning in ["QObject::connect: unexpected warning", "WARNING: ThreadSanitizer: data race",
                        "ERROR: AddressSanitizer: heap-use-after-free", "ERROR: LeakSanitizer: detected memory leaks",
                        "runtime error: unsigned integer overflow", "WARNING: MemorySanitizer: use-of-uninitialized-value"]:
            for version in ["", "6.2.4", "6.4.2"]:
                self.assertEqual(unexpected_stderr(warning, version), [warning])

    def test_singleton_context_warning_is_limited_to_qt62(self):
        warning = "QQmlEngine::setContextForObject(): Object already has a QQmlContext"
        for suffix in ["", "\n", " (qqmlengine.cpp:1209)", " (qqmlengine.cpp:1209)\n"]:
            self.assertEqual(unexpected_stderr(warning + suffix, "6.2.4"), [])
            for version in ["", "6.3.0", "6.4.2", "6.10.2"]:
                self.assertTrue(unexpected_stderr(warning + suffix, version))


if __name__ == "__main__":
    unittest.main()
