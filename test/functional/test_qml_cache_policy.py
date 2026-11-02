#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import contextlib
import io
from pathlib import Path
import sys
import unittest
from unittest import mock

import qml_test_cache_policy


class QmlCachePolicySmokeTests(unittest.TestCase):
    def run_smoke(self, read_cache):
        def launch(_binary, _root, cache_path=None, force_host_cache=False):
            if cache_path is not None:
                self.assertTrue(force_host_cache)
                cache = next(cache_path.glob("*.qmlc"))
                before = cache.stat()
                if read_cache:
                    # A read-only consumer leaves both bytes and mtime intact.
                    self.assertEqual(cache.read_bytes(), b"stale host-generated QML cache")
                self.assertEqual(cache.stat().st_mtime_ns, before.st_mtime_ns)

        with mock.patch.object(qml_test_cache_policy, "find_gui_binary", return_value=Path("unused-gui")), \
                mock.patch.object(qml_test_cache_policy, "launch", side_effect=launch), \
                contextlib.redirect_stdout(io.StringIO()):
            return qml_test_cache_policy.main()

    def test_untouched_cache_passes(self):
        self.assertEqual(self.run_smoke(read_cache=False), 0)

    @unittest.skipUnless(sys.platform.startswith("linux"), "inotify is Linux-only")
    def test_read_without_write_fails(self):
        with self.assertRaisesRegex(AssertionError, "opened or read the stale QML cache"):
            self.run_smoke(read_cache=True)


if __name__ == "__main__":
    unittest.main()
