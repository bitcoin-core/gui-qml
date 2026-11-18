# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from pathlib import Path
import tempfile
import unittest

from qml_test_harness import cleanup_test_directory, create_test_directory


class CleanupTests(unittest.TestCase):
    def test_owned_directory_is_removed(self):
        path = Path(create_test_directory(prefix="qml_cleanup_"))
        (path / "nested").mkdir()
        (path / "nested/file").write_text("fixture", encoding="utf8")
        cleanup_test_directory(path)
        self.assertFalse(path.exists())
        with self.assertRaises(AssertionError):
            cleanup_test_directory(path)

    def test_unowned_and_symlink_targets_are_preserved(self):
        with tempfile.TemporaryDirectory(prefix="qml_cleanup_outside_") as outside:
            with self.assertRaises(AssertionError):
                cleanup_test_directory(outside)
            path = Path(create_test_directory(prefix="qml_cleanup_"))
            saved = path.with_name(path.name + "-saved")
            path.rename(saved)
            try:
                path.symlink_to(outside, target_is_directory=True)
                with self.assertRaises(AssertionError):
                    cleanup_test_directory(path)
                self.assertTrue(Path(outside).is_dir())
                path.unlink()
                path.mkdir()
                with self.assertRaises(AssertionError):
                    cleanup_test_directory(path)
                path.rmdir()
            finally:
                saved.rename(path)
                cleanup_test_directory(path)


if __name__ == "__main__":
    unittest.main()
