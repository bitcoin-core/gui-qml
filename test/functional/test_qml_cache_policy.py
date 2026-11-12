#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import contextlib
import io
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

import qml_test_cache_policy


class QmlCachePolicySmokeTests(unittest.TestCase):
    def run_smoke(self, read_cache):
        def launch(_binary, _root, cache_path=None, force_host_cache=False):
            self.assertIsNotNone(cache_path)
            self.assertTrue(cache_path.is_dir())
            if not force_host_cache:
                self.assertEqual(list(cache_path.iterdir()), [])
            else:
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

    def launch_script(self, source):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            script = root / "gui"
            script.write_text(f"#!{sys.executable}\n" + source, encoding="utf-8")
            script.chmod(0o700)
            with mock.patch.object(qml_test_cache_policy, "STARTUP_SECONDS", 2):
                qml_test_cache_policy.launch(script, root / "launch", root / "cache")

    def test_alive_without_preinit_fails(self):
        with self.assertRaisesRegex(AssertionError, "(?s)did not load preinit.qml.*stalled startup"):
            self.launch_script("import time\nprint('stalled startup', flush=True)\ntime.sleep(60)\n")

    def test_ready_preinit_passes_with_verbose_output(self):
        self.launch_script(
            "import time\nprint('x' * 1048576, flush=True)\n"
            f"print({qml_test_cache_policy.PREINIT_READY!r}, flush=True)\ntime.sleep(60)\n"
        )

    def test_early_exit_reports_output(self):
        with self.assertRaisesRegex(AssertionError, "(?s)exited during startup with 3.*failed startup"):
            self.launch_script("import sys\nprint('failed startup', flush=True)\nsys.exit(3)\n")

    def test_untouched_cache_passes(self):
        self.assertEqual(self.run_smoke(read_cache=False), 0)

    @unittest.skipUnless(sys.platform.startswith("linux"), "inotify is Linux-only")
    def test_read_without_write_fails(self):
        with self.assertRaisesRegex(AssertionError, "opened or read the stale QML cache"):
            self.run_smoke(read_cache=True)


if __name__ == "__main__":
    unittest.main()
