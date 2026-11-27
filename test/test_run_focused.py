#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Unit tests for the focused runner's manifest, command, and report handling."""

from __future__ import annotations

import json
import os
import re
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_focused


class ManifestTests(unittest.TestCase):
    def test_manifest_accepts_three_explicit_suites(self) -> None:
        parsed = run_focused.validate_manifest({
            "version": 1,
            "tests": [
                {"id": "settings", "suite": "unit", "class": "OptionsModelTests",
                 "tests": ["signerValidationKeepsLatestManualIntent"], "expected": 1},
                {"id": "color", "suite": "qml", "file": "tst_formcontrols.qml",
                 "tests": ["FormControls::test_field_surface_error_outlives_focus_and_clears"], "expected": 1},
                {"id": "wire", "suite": "integration",
                 "args": ["--module=wallet_management", "--wallet=enabled", "--test=paymentRequestPersistence"],
                 "expected": 1},
            ],
        })
        self.assertEqual([entry["suite"] for entry in parsed], ["unit", "qml", "integration"])
        self.assertEqual(parsed[0]["tests"], ["signerValidationKeepsLatestManualIntent"])

    def test_manifest_rejects_empty_or_unknown_selection_fields(self) -> None:
        with self.assertRaises(run_focused.RunnerError):
            run_focused.validate_manifest({"version": 1, "tests": []})
        with self.assertRaises(run_focused.RunnerError):
            run_focused.validate_manifest({
                "version": 1,
                "tests": [{"id": "x", "suite": "unit", "class": "OptionsModelTests", "silent": True}],
            })
        for document in (
            {"version": True, "tests": [{"id": "x", "suite": "unit", "class": "OptionsModelTests"}]},
            {"version": 1, "tests": [{"id": "..", "suite": "unit", "class": "OptionsModelTests"}]},
        ):
            with self.subTest(document=document), self.assertRaises(run_focused.RunnerError):
                run_focused.validate_manifest(document)

    def test_manifest_rejects_overlapping_or_duplicate_selectors(self) -> None:
        with self.assertRaises(run_focused.RunnerError):
            run_focused.validate_manifest({"version": 1, "tests": [{
                "id": "x", "suite": "unit", "class": "OptionsModelTests",
                "tests": ["walletWrite", "walletWrite:clear-empty"],
            }]})
        with self.assertRaises(run_focused.RunnerError):
            run_focused.validate_manifest({"version": 1, "tests": [{
                "id": "x", "suite": "qml", "file": "tst_wallet.qml",
                "tests": ["Wallet::copyValue", "Wallet::copyValue"],
            }]})

    def test_manifest_rejects_reporting_and_discovery_arguments(self) -> None:
        for arg in ("-o", "--test-discover", "--list-modules", "-xml", "--output=result.xml"):
            with self.subTest(arg=arg), self.assertRaises(run_focused.RunnerError):
                run_focused.validate_manifest({"version": 1, "tests": [{
                    "id": "x", "suite": "integration", "args": ["--test=case", arg],
                }]})


class UnitBuildScopeTests(unittest.TestCase):
    def test_old_build_does_not_claim_complete_scope(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            self.assertEqual(run_focused.unit_build_scope(Path(temporary)), {"mode": "unrecorded"})

    def test_focused_scope_preserves_requested_and_support_classes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "test").mkdir()
            path = root / "test/unit-build-selection.json"
            path.write_text(json.dumps({"mode": "focused", "requested_classes": ["SelectedTests"],
                                        "compiled_classes": ["SelectedTests", "SharedProbeTests"]}))
            result = run_focused.unit_build_scope(root)
            self.assertEqual(result["mode"], "focused")
            self.assertEqual(result["compiled_classes"], ["SelectedTests", "SharedProbeTests"])
            self.assertEqual(len(result["sha256"]), 64)

    def test_empty_unavailable_and_malformed_scope_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "test").mkdir()
            path = root / "test/unit-build-selection.json"
            for value in ({"mode": "focused", "requested_classes": [], "compiled_classes": []},
                          {"mode": "focused", "requested_classes": ["AbsentTests"], "compiled_classes": ["OtherTests"]},
                          {"mode": "focused", "requested_classes": [3]}, {"mode": "invented"}):
                with self.subTest(value=value), self.assertRaises(run_focused.RunnerError):
                    path.write_text(json.dumps(value))
                    run_focused.unit_build_scope(root)


class CommandAndMetadataTests(unittest.TestCase):
    def test_command_composition_keeps_suite_arguments_and_owns_reports(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            xml = root / "results.xml"
            text = root / "results.txt"
            unit = run_focused.compose_command(
                ["unit-tests", "-platform", "minimal"],
                {"suite": "unit", "class": "OptionsModelTests", "tests": ["writeOutcome:clear"]}, xml, text)
            self.assertEqual(unit, ["unit-tests", "-platform", "minimal", "--test-class=OptionsModelTests",
                                    "writeOutcome:clear", "-o", f"{xml.resolve()},xml", "-o", f"{text.resolve()},txt"])
            qml = run_focused.compose_command(["qml-tests", "-input", ":/tests/tst_wallet.qml"],
                                              {"suite": "qml", "tests": ["Wallet::copyValue"]}, xml, text)
            self.assertEqual(qml[3], "Wallet::copyValue")
            integration = run_focused.compose_command(["integration-tests"],
                {"suite": "integration", "args": ["--module=wallet_management", "--test=createWallet"]}, xml, text)
            self.assertIn("--test=createWallet", integration)
            self.assertEqual(integration[-4:], ["-o", f"{xml.resolve()},xml", "-o", f"{text.resolve()},txt"])

    def test_qml_resolution_uses_registered_input_argument(self) -> None:
        entries = {
            "registered-name-is-not-assumed": {
                "name": "registered-name-is-not-assumed",
                "command": ["/build/bin/qml-tests", "-platform", "minimal", "-input", ":/test/qml/tst_wallet.qml"],
                "properties": {},
            },
        }
        self.assertEqual(run_focused._find_qml_ctest(entries, "tst_wallet.qml")["name"],
                         "registered-name-is-not-assumed")
        with self.assertRaises(run_focused.RunnerError):
            run_focused._find_qml_ctest(entries, "tst_missing.qml")

    def test_unbuilt_unrelated_ctest_target_does_not_block_selected_suites(self) -> None:
        document = {
            "tests": [
                {"name": "bitcoinqml_unit_tests", "command": ["/build/bin/bitcoinqml_unit_tests"], "properties": []},
                {"name": "registered-qml-name", "command": ["/build/bin/bitcoinqml_qmltests", "-input", ":/test/qml/tst_wallet.qml"], "properties": []},
                {"name": "bitcoinqml_bootstrap_tests", "properties": []},
            ],
        }
        entries = run_focused._parse_ctest_entries(json.dumps(document).encode())
        self.assertIsNone(entries["bitcoinqml_bootstrap_tests"]["command"])
        self.assertEqual(entries["bitcoinqml_unit_tests"]["command"][0], "/build/bin/bitcoinqml_unit_tests")
        self.assertEqual(run_focused._find_qml_ctest(entries, "tst_wallet.qml")["name"], "registered-qml-name")

    def test_selected_unbuilt_qml_target_fails_clearly(self) -> None:
        entries = run_focused._parse_ctest_entries(json.dumps({"tests": [
            {"name": "bitcoinqml_qml_missing", "properties": []},
        ]}).encode())
        with self.assertRaisesRegex(run_focused.RunnerError, "has no command"):
            run_focused._find_qml_ctest(entries, "tst_missing.qml")

    def test_ctest_policy_retains_environment_and_fail_expression(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            entry = {"name": "qml-test", "properties": {
                "ENVIRONMENT": ["QT_QPA_PLATFORM=minimal", "QML_DISABLE_DISK_CACHE=1"],
                "TIMEOUT": 120,
                "FAIL_REGULAR_EXPRESSION": "(QWARN|QCRITICAL)[^\\r\\n]*qml:",
            }}
            props = run_focused._entry_properties(entry, Path(temporary), 1500)
            self.assertEqual(props["timeout"], 120)
            self.assertEqual(props["env"]["QT_QPA_PLATFORM"], "minimal")
            matched = run_focused.matching_fail_expressions(props["fail_patterns"], "QWARN qml: warning")
            self.assertEqual(len(matched), 1)
            self.assertEqual(run_focused.matching_fail_expressions(props["fail_patterns"], "ordinary output"), [])

    def test_ctest_fail_expression_checks_warnings_in_text_report(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "stdout.raw").write_text("ordinary stdout\n")
            (root / "stderr.raw").write_text("ordinary stderr\n")
            text = root / "results.txt"
            text.write_text("QWARN qml: warning emitted inside test body\n")
            process = {"stdout": {"path": "stdout.raw"}, "stderr": {"path": "stderr.raw"}}
            output = run_focused.captured_output_text(root, process, text)
            pattern = re.compile(r"QWARN[^\r\n]*qml:")
            self.assertEqual(run_focused.matching_fail_expressions([("qml-warning", pattern)], output), ["qml-warning"])

    def test_unsupported_ctest_policy_fails_instead_of_being_bypassed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaisesRegex(run_focused.RunnerError, "PASS_REGULAR_EXPRESSION"):
                run_focused._entry_properties({"name": "qml-test", "properties": {
                    "PASS_REGULAR_EXPRESSION": "expected text",
                }}, Path(temporary), 30)


class ReportParsingTests(unittest.TestCase):
    def parse(self, xml: str, suite: str = "unit", class_name: str | None = "OptionsModelTests") -> dict:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "results.xml"
            path.write_text(xml)
            return run_focused._parse_result_xml(path, suite, class_name)

    def test_xml_counts_data_rows_not_individual_assertions(self) -> None:
        parsed = self.parse("""<TestCase name="OptionsModelTests">
          <TestFunction name="initTestCase"><Incident type="pass" /></TestFunction>
          <TestFunction name="writeOutcome">
            <Incident type="pass"><DataTag><![CDATA[trim-and-save]]></DataTag></Incident>
            <Incident type="pass"><DataTag><![CDATA[trim-and-save]]></DataTag></Incident>
            <Incident type="pass"><DataTag><![CDATA[clear-empty]]></DataTag></Incident>
          </TestFunction>
          <TestFunction name="cleanupTestCase"><Incident type="pass" /></TestFunction>
        </TestCase>""")
        self.assertEqual(parsed["body_count"], 2)
        self.assertEqual(parsed["passed"], 2)
        self.assertEqual(parsed["identities"], ["writeOutcome:trim-and-save", "writeOutcome:clear-empty"])

    def test_setup_failure_is_retained_separately_from_body_counts(self) -> None:
        parsed = self.parse("""<TestCase name="OptionsModelTests">
          <TestFunction name="initTestCase"><Incident type="fail" /></TestFunction>
          <TestFunction name="aBody"><Incident type="pass" /></TestFunction>
          <TestFunction name="cleanupTestCase"><Incident type="pass" /></TestFunction>
        </TestCase>""")
        self.assertEqual(parsed["body_count"], 1)
        self.assertEqual(parsed["infrastructure_failures"], 1)

    def test_data_row_skip_message_does_not_change_other_rows(self) -> None:
        parsed = self.parse("""<TestCase name="OptionsModelTests">
          <TestFunction name="aBody">
            <Incident type="pass"><DataTag><![CDATA[available]]></DataTag></Incident>
            <Incident type="skip"><DataTag><![CDATA[not-available]]></DataTag><Description>not available</Description></Incident>
            <Message type="qskip"><DataTag><![CDATA[not-available]]></DataTag><Description>not available</Description></Message>
          </TestFunction>
        </TestCase>""")
        self.assertEqual(parsed["body_count"], 2)
        self.assertEqual(parsed["skipped"], 1)
        self.assertEqual(parsed["passed"], 1)

    def test_requested_selectors_reject_unexpected_and_missing_bodies(self) -> None:
        parsed = self.parse("""<TestCase name="OptionsModelTests">
          <TestFunction name="wanted"><Incident type="pass" /></TestFunction>
          <TestFunction name="extra"><Incident type="pass" /></TestFunction>
        </TestCase>""")
        issues = run_focused._validate_selected_identities(
            {"suite": "unit", "tests": ["wanted"]}, parsed)
        self.assertTrue(any("unexpected" in issue for issue in issues))
        missing = run_focused._validate_selected_identities(
            {"suite": "unit", "tests": ["wanted:absent"]}, parsed)
        self.assertTrue(any("produced no body" in issue for issue in missing))

    def test_duplicate_body_identity_is_rejected(self) -> None:
        with self.assertRaisesRegex(run_focused.RunnerError, "duplicate selected body"):
            self.parse("""<TestCase name="OptionsModelTests">
              <TestFunction name="same"><Incident type="pass" /></TestFunction>
              <TestFunction name="same"><Incident type="pass" /></TestFunction>
            </TestCase>""")


class ProcessTests(unittest.TestCase):
    def test_process_return_and_raw_output_are_reported(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            process = run_focused._run_process(
                [sys.executable, "-c", "import sys; print('out'); print('err', file=sys.stderr); sys.exit(7)"],
                root, {}, 10, root, "child")
            self.assertEqual(process["return_code"], 7)
            self.assertEqual((root / process["stdout"]["path"]).read_text(), "out\n")
            self.assertEqual((root / process["stderr"]["path"]).read_text(), "err\n")
            self.assertTrue(run_focused.process_failure_issues(process))

    def test_process_timeout_is_explicit(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            process = run_focused._run_process(
                [sys.executable, "-c", "import time; time.sleep(1)"], root, {}, 0.01, root, "timeout")
            self.assertTrue(process["timed_out"])
            self.assertIsNone(process["return_code"])
            self.assertTrue(run_focused.process_failure_issues(process))

    @unittest.skipUnless(os.name == "posix", "Unix process-group cleanup")
    def test_timeout_terminates_application_descendants(self) -> None:
        # The child retains stdout after its parent exits. Its backup watchdog
        # keeps a broken implementation finite and exposes survival explicitly.
        child = "import threading; print('descendant-ready', flush=True); threading.Event().wait(2); print('survived-timeout', flush=True)"
        parent = f"import subprocess,sys,threading; subprocess.Popen([sys.executable, '-c', {child!r}]); threading.Event().wait(10)"
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            process = run_focused._run_process([sys.executable, "-c", parent], root, {}, 1, root, "descendants")
            self.assertTrue(process["timed_out"])
            output = (root / process["stdout"]["path"]).read_text()
            self.assertIn("descendant-ready", output)
            self.assertNotIn("survived-timeout", output)


if __name__ == "__main__":
    unittest.main()
