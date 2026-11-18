#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Check clang-tidy scope selection without compiling or running analysis."""

import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


DRIVER = Path(__file__).resolve().parents[1] / "ci/clang_tidy.py"
SPEC = importlib.util.spec_from_file_location("app_clang_tidy", DRIVER)
TIDY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TIDY)


class ClangTidySelectionTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.workspace = Path(self.temporary.name).resolve()
        self.root = self.workspace / "app"
        self.build = self.root / "build"
        self.cpp = self.touch("qml/selected.cpp")
        self.other = self.touch("qml/other.cpp")
        self.objc = self.touch("qml/platform.mm")
        self.header = self.touch("qml/selected.h")
        self.core = self.touch("bitcoin/src/core.cpp")
        self.generated = self.touch("build/qrc_generated.cpp")
        self.mocs = [self.touch("build/bitcoinqml_autogen/mocs_compilation.cpp"),
                     self.touch("build/test/bitcoinqml_unit_tests_autogen/mocs_compilation.cpp")]
        self.core_moc = self.touch("build/bitcoin/src/qt/bitcoinqt_autogen/mocs_compilation.cpp")
        self.database = [self.entry(path) for path in
                         [self.cpp, self.other, self.objc, self.header, self.core, self.generated, *self.mocs, self.core_moc]]

    def touch(self, name):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.touch()
        return path

    def entry(self, path, command="configured compiler command"):
        return {"directory": str(self.build), "file": os.path.relpath(path, self.build), "command": command}

    def select(self, files=None, database=None):
        return TIDY.select_entries(self.database if database is None else database, self.root, self.build, files)

    def test_selected_cpp_keeps_every_app_moc(self):
        selected = self.select([Path("qml/selected.cpp")])
        self.assertEqual([entry["file"] for entry in selected], [str(self.cpp), *map(str, self.mocs)])
        self.assertNotIn(str(self.core_moc), [entry["file"] for entry in selected])

    def test_default_checks_all_handwritten_app_units_and_mocs(self):
        selected = self.select()
        self.assertEqual([entry["file"] for entry in selected], [str(self.cpp), str(self.other), str(self.objc), *map(str, self.mocs)])

    def test_absolute_objc_and_relative_cpp_are_supported(self):
        selected = self.select([self.objc, Path("qml/selected.cpp")])
        self.assertEqual([entry["file"] for entry in selected], [str(self.cpp), str(self.objc), *map(str, self.mocs)])

    def test_duplicate_requests_and_compilation_entries_are_harmless(self):
        database = [*self.database, self.entry(self.cpp, "different target")]
        selected = self.select([self.cpp, Path("qml/selected.cpp"), self.cpp], database)
        self.assertEqual(len(selected), 3)
        self.assertEqual(selected[0]["command"], "configured compiler command")
        self.assertEqual(self.database[0]["file"], "../qml/selected.cpp")

    def test_existing_uncompiled_cpp_cannot_pass_on_mocs_alone(self):
        unknown = self.touch("qml/not_configured.cpp")
        with self.assertRaisesRegex(TIDY.FileSelectionError, "not configured compilation units.*qml/not_configured.cpp"):
            self.select([unknown])

    def test_missing_cpp_is_rejected(self):
        with self.assertRaisesRegex(TIDY.FileSelectionError, "does not exist"):
            self.select([Path("qml/missing.cpp")])

    def test_header_is_rejected_even_when_present_in_database(self):
        with self.assertRaisesRegex(TIDY.FileSelectionError, r"handwritten .cpp or .mm"):
            self.select([self.header])

    def test_core_outside_and_generated_files_are_rejected(self):
        outside = self.workspace / "outside.cpp"
        outside.touch()
        for path, message in [(self.core, "Core files"), (outside, "outside the app root"),
                              (self.generated, "Generated build files"), (self.mocs[0], "Generated build files")]:
            with self.subTest(path=path), self.assertRaisesRegex(TIDY.FileSelectionError, message):
                self.select([path])

    def test_explicit_empty_selection_does_not_become_default(self):
        with self.assertRaisesRegex(TIDY.FileSelectionError, "selection is empty"):
            self.select([])

    def test_empty_default_database_is_not_a_pass(self):
        with self.assertRaisesRegex(TIDY.FileSelectionError, "No app compilation units"):
            self.select(database=[])

    def copy_driver(self):
        target = self.root / "ci/clang_tidy.py"
        target.parent.mkdir()
        shutil.copyfile(DRIVER, target)
        return target

    def test_cli_empty_files_option_is_a_usage_error(self):
        result = subprocess.run([sys.executable, str(self.copy_driver()), "--build-dir", str(self.build), "--files"],
                                cwd=self.workspace, capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 2)
        self.assertIn("expected at least one argument", result.stderr)
        self.assertNotIn("Traceback", result.stderr)

    def test_cli_rejects_paths_before_reading_core_config_or_building_plugin(self):
        driver = self.copy_driver()
        for name, message in [("qml/selected.h", "handwritten .cpp or .mm"), ("bitcoin/src/core.cpp", "Core files"),
                              ("qml/missing.cpp", "does not exist"), ("../outside.cpp", "outside the app root")]:
            with self.subTest(name=name):
                result = subprocess.run([sys.executable, str(driver), "--build-dir", str(self.build), "--files", name],
                                        cwd=self.workspace, capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 2)
                self.assertIn(message, result.stderr)
                self.assertNotIn("Traceback", result.stderr)
                self.assertFalse((self.build / "bitcoin-tidy").exists())
        self.assertFalse((self.root / "bitcoin/ci").exists())

    def test_cli_rejects_uncompiled_cpp_using_real_ninja_inventory(self):
        if shutil.which("ninja") is None:
            self.skipTest("Ninja is required to read the configured compilation inventory")
        driver = self.copy_driver()
        self.touch("qml/not_configured.cpp")
        (self.build / "build.ninja").write_text(
            "rule compile\n  command = compiler-must-not-run -c $in -o $out\n"
            "build selected.o: compile ../qml/selected.cpp\n")
        result = subprocess.run([sys.executable, str(driver), "--build-dir", str(self.build),
                                 "--files", "qml/not_configured.cpp"], cwd=self.workspace,
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 2)
        self.assertIn("not configured compilation units: qml/not_configured.cpp", result.stderr)
        self.assertNotIn("Traceback", result.stderr)
        self.assertFalse((self.build / "selected.o").exists())
        self.assertFalse((self.build / "bitcoin-tidy").exists())


if __name__ == "__main__":
    unittest.main()
