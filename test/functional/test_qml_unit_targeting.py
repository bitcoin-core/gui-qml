#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Check class discovery/targeting using the actual bitcoinqml_unit_tests binary.

--list-test-classes prints every registered class before QApplication, Core
setup or test construction. --test-class=NAME selects one class and leaves
QtTest function/data-row arguments intact. It overrides a valid legacy
BITCOIN_QML_TEST_CLASSES selection, but all supplied names must be registered.
An empty legacy environment remains unfiltered; an explicit empty CLI selector
is an error. Existing fatal/logging child modes retain their original precedence.
"""

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time
import unittest
import xml.etree.ElementTree as ET


def disable_core_files():
    # The invalid-platform control intentionally fails before QApplication
    # construction completes. Keep that control from producing a core dump.
    import resource  # pylint: disable=import-outside-toplevel
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


class UnitTargetingTests(unittest.TestCase):
    binary: str = ""
    retained_output = None

    @classmethod
    def setUpClass(cls):
        cls.temporary = None
        if cls.retained_output is None:
            cls.temporary = tempfile.TemporaryDirectory(prefix="qml-unit-targeting-")
            cls.output = Path(cls.temporary.name)
        else:
            cls.output = cls.retained_output
            cls.output.mkdir(parents=True, exist_ok=False)
        cls.runtime = cls.output / "runtime"
        cls.runtime.mkdir(mode=0o700)
        cls.process_count = 0

    @classmethod
    def tearDownClass(cls):
        if cls.temporary is not None:
            cls.temporary.cleanup()

    def run_binary(self, arguments, extra_environment=None):
        type(self).process_count += 1
        output = self.output / f"process-{self.process_count:02d}"
        output.mkdir()
        environment = dict(os.environ)
        for name in (
            "BITCOIN_QML_TEST_CLASSES", "BITCOIN_QML_QTLOG_SINK_CASE",
            "BITCOIN_QML_QTLOG_CONFIGURED_CASE", "BITCOIN_QML_QTLOG_FATAL_CASE",
            "BITCOIN_QML_FATAL_EXCEPTION_CHILD",
        ):
            environment.pop(name, None)
        environment.update(
            QT_QPA_PLATFORM="minimal", QT_QUICK_BACKEND="software",
            QT_MESSAGE_PATTERN="[%{type}] %{category}: %{message}",
            XDG_RUNTIME_DIR=str(self.runtime),
        )
        environment.update(extra_environment or {})
        command = [self.binary, *arguments]
        record = {
            "command": command, "cwd": str(output),
            "environment": {name: environment[name] for name in (
                "BITCOIN_QML_TEST_CLASSES", "QT_QPA_PLATFORM", "QT_MESSAGE_PATTERN",
            ) if name in environment},
        }
        start = time.monotonic()
        timed_out = False
        with subprocess.Popen(
            command, cwd=output, env=environment, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, text=True, encoding="utf-8", errors="replace",
            preexec_fn=disable_core_files if os.name == "posix" else None,
        ) as process:
            try:
                stdout, stderr = process.communicate(timeout=10)
            except subprocess.TimeoutExpired:
                timed_out = True
                process.kill()
                stdout, stderr = process.communicate()
            result = subprocess.CompletedProcess(command, process.returncode, stdout, stderr)
        record.update(actual_exit_code=result.returncode, timed_out=timed_out, seconds=time.monotonic() - start)
        (output / "stdout").write_text(result.stdout, encoding="utf-8")
        (output / "stderr").write_text(result.stderr, encoding="utf-8")
        (output / "process.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
        self.assertFalse(timed_out, f"Targeted process timed out: {command}: {result.stdout}{result.stderr}")
        return result, output

    def assert_one_body(self, arguments, function, tag="", environment=None, xml_name="results.xml"):
        result, output = self.run_binary([
            *arguments, "-o", f"{xml_name},xml", "-o", "-,txt", "-platform", "minimal",
        ], environment)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Totals:", result.stdout)
        self.assertIsNone(re.search(r"^(QWARN|QCRITICAL|QFATAL)\s*:", result.stdout, re.MULTILINE), result.stdout)
        # These two existing qDebug messages occur after QtTest logging closes.
        # Preserve their output and reject every other stderr message.
        lifecycle_debug = (
            "[debug] default: shutdownAll : Waiting for executors QList() and pending thread joins\n"
            "[debug] default: All tracked background workers drained\n"
        )
        self.assertIn(result.stderr, ("", lifecycle_debug), result.stderr)
        root = ET.parse(output / xml_name).getroot()
        self.assertEqual(root.tag, "TestCase")
        self.assertEqual(root.get("name"), "AmountParseTests")
        rows = []
        for test in root.findall("TestFunction"):
            incidents = test.findall("Incident")
            self.assertTrue(incidents, test.get("name"))
            for incident in incidents:
                self.assertEqual(incident.get("type"), "pass", ET.tostring(incident, encoding="unicode"))
                rows.append((test.get("name"), incident.findtext("DataTag", "")))
        self.assertCountEqual(rows, [("initTestCase", ""), (function, tag), ("cleanupTestCase", "")])
        for message in root.findall(".//Message"):
            self.assertIn(message.get("type", "").lower(), ("qdebug", "qinfo", "info"), ET.tostring(message, encoding="unicode"))

    def test_discovery_needs_no_application_or_test_objects(self):
        for legacy in (None, "", ",,"):
            with self.subTest(legacy=legacy):
                environment = {"QT_QPA_PLATFORM": "r10-invalid-platform-for-targeting"}
                if legacy is not None:
                    environment["BITCOIN_QML_TEST_CLASSES"] = legacy
                result, _ = self.run_binary([
                    "--list-test-classes", "-platform", "r10-invalid-platform-for-targeting",
                ], environment)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual(result.stderr, "")
                names = result.stdout.splitlines()
                self.assertTrue(names)
                self.assertEqual(names, sorted(set(names)))
                self.assertTrue(all(re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", name) for name in names), result.stdout)
                self.assertIn("AmountParseTests", names)
                self.assertIn("ExecutionTests", names)

    def test_cli_preserves_function_selection(self):
        self.assert_one_body(
            ["--test-class=AmountParseTests", "defaultResultRejectsAnAmount"],
            "defaultResultRejectsAnAmount",
        )

    def test_cli_preserves_exact_data_row_selection(self):
        self.assert_one_body(
            ["--test-class=AmountParseTests", "exactInput:btc-one-sat"],
            "exactInput", "btc-one-sat",
        )

    def test_environment_selection_remains_supported(self):
        self.assert_one_body(
            ["exactInput:btc-dot"], "exactInput", "btc-dot",
            environment={"BITCOIN_QML_TEST_CLASSES": "AmountParseTests"},
        )

    def test_cli_overrides_a_valid_environment_selection(self):
        self.assert_one_body(
            ["--test-class=AmountParseTests", "defaultResultRejectsAnAmount"],
            "defaultResultRejectsAnAmount", environment={"BITCOIN_QML_TEST_CLASSES": "ExecutionTests"},
        )

    def test_qttest_output_filename_is_not_a_runner_selector(self):
        self.assert_one_body(
            ["--test-class=AmountParseTests", "defaultResultRejectsAnAmount"],
            "defaultResultRejectsAnAmount", xml_name="--test-class=opaque-filename.xml",
        )

    def test_invalid_selectors_fail_before_application_initialization(self):
        cases = (
            (["--test-class=NoSuchQtTestClass"], {}, "Unknown Qt test class: NoSuchQtTestClass"),
            (["--test-class="], {}, "requires a nonempty class name"),
            (["--test-class="], {"BITCOIN_QML_TEST_CLASSES": "AmountParseTests"}, "requires a nonempty class name"),
            ([], {"BITCOIN_QML_TEST_CLASSES": "NoSuchQtTestClass"}, "Unknown Qt test class: NoSuchQtTestClass"),
            ([], {"BITCOIN_QML_TEST_CLASSES": "AmountParseTests,NoSuchQtTestClass"}, "Unknown Qt test class: NoSuchQtTestClass"),
            (["--test-class=AmountParseTests"], {"BITCOIN_QML_TEST_CLASSES": "ExecutionTests,NoSuchQtTestClass"}, "Unknown Qt test class: NoSuchQtTestClass"),
            (["--test-class=AmountParseTests", "--test-class=ExecutionTests"], {}, "Only one --test-class=NAME selector"),
            (["--test-class"], {}, "Use --test-class=NAME"),
        )
        for arguments, environment, diagnostic in cases:
            with self.subTest(arguments=arguments, environment=environment):
                environment = dict(environment, QT_QPA_PLATFORM="r10-invalid-platform-for-targeting")
                result, output = self.run_binary([
                    *arguments, "-o", "must-not-exist.xml,xml", "-platform", "r10-invalid-platform-for-targeting",
                ], environment)
                self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                self.assertEqual(result.stdout, "")
                self.assertIn(diagnostic, result.stderr)
                self.assertNotIn("r10-invalid-platform-for-targeting", result.stderr)
                self.assertFalse((output / "must-not-exist.xml").exists())

    def test_invalid_platform_control_rejects_real_application_startup(self):
        result, output = self.run_binary([
            "--test-class=AmountParseTests", "defaultResultRejectsAnAmount",
            "-o", "must-not-exist.xml,xml", "-platform", "r10-invalid-platform-for-targeting",
        ], {"QT_QPA_PLATFORM": "r10-invalid-platform-for-targeting"})
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("r10-invalid-platform-for-targeting", result.stderr)
        self.assertNotIn("Totals:", result.stdout)
        self.assertFalse((output / "must-not-exist.xml").exists())
        for diagnostic in (
            "ERROR: AddressSanitizer:", "ERROR: LeakSanitizer:",
            "WARNING: ThreadSanitizer:", "WARNING: MemorySanitizer:", "runtime error:",
        ):
            self.assertNotIn(diagnostic, result.stderr)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path, help="Real bitcoinqml_unit_tests binary")
    parser.add_argument("--output-dir", type=Path, help="Retain per-process stdout/stderr/XML in this new directory")
    args = parser.parse_args()
    UnitTargetingTests.binary = str(args.binary.resolve(strict=True))
    UnitTargetingTests.retained_output = args.output_dir.resolve() if args.output_dir else None
    unittest.main(argv=[sys.argv[0]], verbosity=2)
