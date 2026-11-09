#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Verify that functional-test cleanup only removes owned directories."""

import os
from pathlib import Path
import tempfile
import unittest

from qml_test_harness import cleanup_test_directory, create_test_directory


class TestDirectories(unittest.TestCase):
    def test_owned_directory_is_removed(self):
        directory = create_test_directory(prefix="qml-cleanup-")
        (Path(directory) / "data").write_text("test", encoding="utf-8")
        cleanup_test_directory(directory)
        self.assertFalse(Path(directory).exists())

    def test_unowned_directory_is_preserved(self):
        with tempfile.TemporaryDirectory(prefix="qml-unowned-") as directory:
            sentinel = Path(directory) / "sentinel"
            sentinel.write_text("keep", encoding="utf-8")
            with self.assertRaises(AssertionError):
                cleanup_test_directory(directory)
            self.assertEqual(sentinel.read_text(encoding="utf-8"), "keep")

    @unittest.skipUnless(hasattr(os, "symlink"), "Requires symlinks")
    def test_replaced_directory_is_refused(self):
        directory = create_test_directory(prefix="qml-cleanup-")
        original = directory + "-original"
        with tempfile.TemporaryDirectory(prefix="qml-unowned-") as outside:
            sentinel = Path(outside) / "sentinel"
            sentinel.write_text("keep", encoding="utf-8")
            os.rename(directory, original)
            try:
                os.symlink(outside, directory, target_is_directory=True)
                with self.assertRaises(AssertionError):
                    cleanup_test_directory(directory)
                self.assertTrue(sentinel.exists())
            finally:
                if os.path.islink(directory):
                    os.unlink(directory)
                os.rename(original, directory)
                cleanup_test_directory(directory)


if __name__ == "__main__":
    unittest.main()
