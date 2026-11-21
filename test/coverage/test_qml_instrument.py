#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Pinned-Qt controls for explicitly supported QML instrumentation forms.

Run with the CI Qt tools present. Missing tools fail rather than silently skip.
These controls do not certify complete textual-line or implicit-edge coverage.
"""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

import qml_instrument as instrument


QT = Path(os.environ.get("QML_COVERAGE_QT_TOOLS", "/usr/lib/qt6/bin"))
PROBE = '''
    property var hits: []
    property var __r10cov: ({
        line: function(id) { hits.push([id, "line"]); },
        truth: function(id, value) { hits.push([id, Boolean(value)]); return value; },
        nullish: function(id, value) { hits.push([id, value === null || value === undefined]); return value; }
    })
'''


class MappingSupportTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="qml-cov-contract-", dir=os.environ.get("QML_COVERAGE_TEST_TMPDIR"))
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)

    def map(self, source, metadata_only=False):
        path = self.directory / "Subject.qml"
        path.write_text(source)
        dump = instrument.run_tool([str(QT / "qmldom"), "--dump-ast", str(path)], ast_dump=True)
        tree = instrument.parse_dump(dump)
        tool = instrument.Instrumenter("Subject.qml", source, tree, 0, metadata_only)
        transformed = tool.run()
        path.write_text(transformed)
        instrument.run_tool([str(QT / "qmlformat"), str(path)])
        instrument.parse_dump(instrument.run_tool([str(QT / "qmldom"), "--dump-ast", str(path)], ast_dump=True))
        return tool, transformed

    def run_qml(self, source, transformed, body, expected_success=True):
        transformed = transformed.replace("QtObject {", "QtObject {" + PROBE, 1)
        test = '''import QtQuick
import QtTest
TestCase {
    name: "InstrumentationSupport"
    function test_semantics() {
        const original = Qt.createQmlObject(%s, this);
        const measured = Qt.createQmlObject(%s, this);
        %s
        original.destroy(); measured.destroy();
    }
}
''' % (json.dumps(source), json.dumps(transformed), body)
        path = self.directory / "tst_semantics.qml"
        path.write_text(test)
        result = subprocess.run([str(QT / "qmltestrunner"), "-platform", "minimal", "-input", str(path)],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=60)
        if retained := os.environ.get("QML_COVERAGE_CONTROL_OUTPUT"):
            output = Path(retained) / (self._testMethodName + ("-positive" if expected_success else "-negative"))
            output.mkdir(parents=True, exist_ok=False)
            (output / "stdout").write_text(result.stdout)
            (output / "stderr").write_text(result.stderr)
            (output / "fixture.qml").write_text(test)
            (output / "result.json").write_text(json.dumps({"command": result.args, "exit": result.returncode,
                                                          "expected_success": expected_success}, indent=2) + "\n")
        self.assertEqual(result.returncode == 0, expected_success, result.stdout + result.stderr)
        if not expected_success:
            self.assertIn("FAIL!", result.stdout)

    def test_trailing_default_decisions_and_semantics(self):
        source = '''import QtQml
QtObject {
    property var fixtureValue
    function select(value = fixtureValue.value) { return [value, arguments.length]; }
    function trailing(prefix, value = -1) { return [prefix, value, arguments.length]; }
    function recurse(n, value = -1) { if (n === 0) return value; return recurse(n - 1); }
}
'''
        tool, transformed = self.map(source)
        defaults = [point for point in tool.points if point["role"] == "trailing-default-parameter"]
        self.assertEqual(len(defaults), 3)
        first, last, recursive = [point["id"] for point in defaults]
        body = '''
        let originalReads = 0; let measuredReads = 0;
        original.fixtureValue = ({ get value() { ++originalReads; return undefined; } });
        measured.fixtureValue = ({ get value() { ++measuredReads; return undefined; } });
        compare(JSON.stringify(measured.select()), JSON.stringify(original.select()));
        compare(JSON.stringify(measured.select(undefined)), JSON.stringify(original.select(undefined)));
        for (const value of [null, false, 0, "", 23])
            compare(JSON.stringify(measured.select(value)), JSON.stringify(original.select(value)));
        compare(originalReads, 2); compare(measuredReads, 2);
        compare(measured.select.length, original.select.length);
        original.fixtureValue = ({ get value() { ++originalReads; throw "getter-error"; } });
        measured.fixtureValue = ({ get value() { ++measuredReads; throw "getter-error"; } });
        let originalError; let measuredError;
        try { original.select(); } catch (error) { originalError = error; }
        try { measured.select(); } catch (error) { measuredError = error; }
        compare(measuredError, originalError); compare(measuredError, "getter-error");
        compare(originalReads, 3); compare(measuredReads, 3);
        compare(JSON.stringify(measured.trailing("p")), JSON.stringify(original.trailing("p")));
        compare(JSON.stringify(measured.trailing("p", undefined)), JSON.stringify(original.trailing("p", undefined)));
        compare(JSON.stringify(measured.trailing("p", null)), JSON.stringify(original.trailing("p", null)));
        compare(measured.recurse(2, 8), original.recurse(2, 8));
        const hits = function(id, outcome) { return measured.hits.filter(function(hit) { return hit[0] === id && hit[1] === outcome; }).length; };
        compare(hits(%d, true), 3); compare(hits(%d, false), 5);
        compare(hits(%d, true), 2); compare(hits(%d, false), 1);
        compare(hits(%d, true), 2); compare(hits(%d, false), 1);
        ''' % (first, first, last, last, recursive, recursive)
        self.run_qml(source, transformed, body)
        # The independent semantic oracle must detect a corrupted transparent
        # probe; parsing and nonempty counters alone do not qualify a mapping.
        self.run_qml(source, transformed.replace(f".truth({first},true)", f".truth({first},false)"), body, expected_success=False)

    def test_unproved_parameter_forms_reject(self):
        declarations = [
            "function a(x = 1, y) { return x; }",
            "function a(x = 1, y = 2) { return x; }",
            "property var a: (x = 1) => x",
            "function a({x}) { return x; }",
            "function a(x = unknown()) { return x; }",
            "function a(arguments, x = 1) { return x; }",
            "function a(x = arguments[0]) { return x; }",
        ]
        for declaration in declarations:
            with self.subTest(declaration=declaration), self.assertRaises(instrument.MappingError):
                self.map("import QtQml\nQtObject { " + declaration + " }\n")

    def test_delete_keeps_reference_and_effects(self):
        source = '''import QtQml
QtObject {
    function erase(object, key) { return delete object[key]; }
    function conditional(object, condition) { return condition ? delete object.a : delete object.b; }
}
'''
        _, transformed = self.map(source)
        oracle = '''
        let originalReads = 0; let measuredReads = 0;
        let left = {}; let right = {};
        Object.defineProperty(left, "value", { configurable: true, get: function() { ++originalReads; return 4; } });
        Object.defineProperty(right, "value", { configurable: true, get: function() { ++measuredReads; return 4; } });
        compare(measured.erase(right, "value"), original.erase(left, "value"));
        compare(originalReads, 0); compare(measuredReads, 0);
        verify(!("value" in left)); verify(!("value" in right));
        for (const condition of [true, false]) {
            left = {a: 1, b: 2}; right = {a: 1, b: 2};
            compare(measured.conditional(right, condition), original.conditional(left, condition));
            compare(JSON.stringify(right), JSON.stringify(left));
        }
        '''
        self.run_qml(source, transformed, oracle)
        self.assertIn("delete object[key]", transformed)
        self.run_qml(source, transformed.replace("delete object[key]", "(function(value) { return true; })(object[key])"),
                     oracle, expected_success=False)

    def test_annotations_are_metadata(self):
        source = '''import QtQml
QtObject {
    function typed(value: int): int { return value + 1; }
}
'''
        tool, transformed = self.map(source)
        self.assertEqual(sum(entry["kind"] == "qml-type-annotation" for entry in tool.metadata), 2)
        self.run_qml(source, transformed, "for (const value of [0, 8, -1]) compare(measured.typed(value), original.typed(value));")

    def test_native_list_roles_preserve_values_and_remain_unmeasured(self):
        source = '''import QtQuick
QtObject {
    property ListModel choices: ListModel {
        ListElement { label: qsTr("High"); target: 1 }
        ListElement { label: qsTr("Low"); target: -1 }
    }
    function snapshot() { return JSON.stringify([choices.count, choices.get(0).label, choices.get(0).target, choices.get(1).label, choices.get(1).target]); }
}
'''
        tool, transformed = self.map(source)
        self.assertEqual(len(tool.native_initializers), 4)
        for entry in tool.native_initializers:
            self.assertIn("unmeasured", entry["measurement"])
            self.assertFalse(any(entry["start_utf16"] <= point["start_utf16"] < entry["end_utf16"] for point in tool.points))
        self.assertIn('ListElement { label: qsTr("High"); target: 1 }', transformed)
        oracle = 'compare(measured.snapshot(), original.snapshot()); compare(measured.choices.get(0).label, "High");'
        self.run_qml(source, transformed, oracle)
        self.run_qml(source, transformed.replace('qsTr("High")', 'qsTr("Wrong")'), oracle, expected_success=False)
        for value in ["condition ? 1 : 2", "qsTr(variable)"]:
            with self.subTest(value=value), self.assertRaises(instrument.MappingError):
                self.map(source.replace('qsTr("High")', value))

    def test_explicit_metadata_disposition_has_structural_proof(self):
        for relative in ["qml/pages/wallet/WalletNavigationTab.qml", "qml/compat/qt65/AppSettings.qml"]:
            source = (Path(__file__).resolve().parents[2] / relative).read_text()
            with self.subTest(relative=relative):
                with self.assertRaises(instrument.MappingError):
                    self.map(source)
                tool, transformed = self.map(source, metadata_only=True)
                self.assertEqual(transformed, source)
                self.assertFalse(tool.points)
                self.assertEqual(tool.metadata[0]["source_sha256"], instrument.digest(source.encode()))
        for source in ["import QtQml\nQtObject { property int value: 3 }",
                       "import QtQml\nQtObject { function work() { return 3; } }"]:
            with self.subTest(source=source), self.assertRaises(instrument.MappingError):
                self.map(source, metadata_only=True)


if __name__ == "__main__":
    unittest.main()
