#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Exercise the real CMake source selector, keeping the existing Qt harness."""

import json
from pathlib import Path
import subprocess
import tempfile
import unittest


class FocusedUnitBuildTests(unittest.TestCase):
    def configure(self, selection=None):
        with tempfile.TemporaryDirectory(prefix="qml-unit-build-") as temporary:
            root = Path(temporary)
            module = Path(__file__).resolve().parent.parent / "cmake" / "FocusedUnitTests.cmake"
            (root / "CMakeLists.txt").write_text(
                'cmake_minimum_required(VERSION 3.22)\nproject(Selection NONE)\n'
                'add_custom_target(unit SOURCES main.cpp support.cpp alpha.cpp beta.cpp)\n'
                f'include("{module.as_posix()}")\n'
                'configure_focused_unit_build(unit "${CMAKE_BINARY_DIR}/scope.json" SUPPORT_SOURCES support.cpp)\n'
                'get_target_property(selected unit SOURCES)\n'
                'file(WRITE "${CMAKE_BINARY_DIR}/sources.txt" "${selected}")\n', encoding="utf-8")
            for name, classes in {"main": [], "support": ["SupportTests"],
                                  "alpha": ["AlphaTests", "CompanionTests"], "beta": ["BetaTests"]}.items():
                (root / f"{name}.cpp").write_text("\n".join(
                    f"BITCOINQML_REGISTER_QT_TEST({cls})" for cls in classes) + "\n", encoding="utf-8")
            command = ["cmake", "-S", str(root), "-B", str(root / "build")]
            if selection is not None:
                command.append(f"-DAPP_UNIT_TEST_CLASSES={selection}")
            result = subprocess.run(command, capture_output=True, text=True, timeout=30, check=False)
            if result.returncode:
                return result, None, None
            scope = json.loads((root / "build/scope.json").read_text(encoding="utf-8"))
            sources = (root / "build/sources.txt").read_text(encoding="utf-8").split(";")
            return result, scope, sources

    def test_default_and_explicit_reset_preserve_all_sources(self):
        for selection in (None, ""):
            with self.subTest(selection=selection):
                result, scope, sources = self.configure(selection)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(scope, {"mode": "complete", "requested_classes": []})
                self.assertEqual(sources, ["main.cpp", "support.cpp", "alpha.cpp", "beta.cpp"])

    def test_selection_keeps_shared_support_and_companion_classes(self):
        result, scope, sources = self.configure("AlphaTests")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(sources, ["main.cpp", "support.cpp", "alpha.cpp"])
        self.assertEqual(scope, {"mode": "focused", "requested_classes": ["AlphaTests"],
                                 "compiled_classes": ["AlphaTests", "CompanionTests", "SupportTests"]})

    def test_unavailable_class_cannot_produce_an_empty_success(self):
        result, _, _ = self.configure("AlphaTests;UnavailableWalletTests")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unavailable in this configuration", result.stderr)

    def test_invalid_duplicate_and_empty_names_are_rejected(self):
        for selection in ("AlphaTests;AlphaTests", "AlphaTests;;BetaTests", ";", "AlphaTests;", "../alpha.cpp"):
            with self.subTest(selection=selection):
                result, _, _ = self.configure(selection)
                self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
