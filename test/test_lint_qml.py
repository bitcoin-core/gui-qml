#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Exercise the QML lint gate against actual valid and invalid resource trees."""

import argparse
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qmllint", required=True)
    parser.add_argument("--imports", required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    args = parser.parse_args()
    args.build_dir.mkdir(parents=True, exist_ok=True)
    cases = {
        "resource-alias": ('import "../controls"\nBase { width: 12 }', 0),
        "qt62-component-capture": (
            'import QtQuick\nimport "../controls"\n'
            'Base { id: root; property int value: 12; '
            'Repeater { model: 1; Item { width: root.value } } }', 0),
        "qt62-property-changes": (
            'import QtQuick\nimport "../controls"\n'
            'Base { id: root; states: State { name: "active"; '
            'PropertyChanges { target: root; width: 12 } } }', 0),
        "required-model-provided": ('import QtQuick\nimport "../controls"\nItem { RequiredModel { settingsModel: ({}) } }', 0),
        "required-model-missing": ('import QtQuick\nimport "../controls"\nItem { RequiredModel {} }', 1),
        "unknown-name": ('import "../controls"\nBase { width: missingName }', 1),
        "capture-and-unknown-name": (
            'import QtQuick\nimport "../controls"\n'
            'Base { id: root; property int value: 12; '
            'Repeater { model: 1; Item { width: root.value; height: missingName } } }', 1),
        "unqualified-property": (
            'import QtQuick\nimport "../controls"\n'
            'Base { property int value: 12; Item { width: value } }', 1),
        "missing-property": ('import "../controls"\nBase { nonexistent: 12 }', 1),
        "missing-type": ('import "../controls"\nNonexistent {}', 1),
        "unused-import": ('import QtQuick.Controls\nimport "../controls"\nBase {}', 1),
        "syntax-error": ('import "../controls"\nBase {', 1),
    }
    with tempfile.TemporaryDirectory(prefix="lint-gate-test-", dir=args.build_dir) as temporary:
        tree = Path(temporary)
        (tree / "base.qml").write_text("import QtQuick\nItem {}\n", encoding="utf-8")
        (tree / "required.qml").write_text(
            "import QtQuick\nItem { required property var settingsModel }\n", encoding="utf-8")
        resource = tree / "resources.qrc"
        resource.write_text('<RCC><qresource prefix="/qml">'
                            '<file alias="controls/Base.qml">base.qml</file>'
                            '<file alias="controls/RequiredModel.qml">required.qml</file>'
                            '<file alias="pages/UseBase.qml">use.qml</file>'
                            '</qresource></RCC>', encoding="utf-8")
        for name, (source, expected) in cases.items():
            (tree / "use.qml").write_text(source + "\n", encoding="utf-8")
            command = [sys.executable, str(Path(__file__).with_name("lint_qml.py")),
                       "--qmllint", args.qmllint, "--imports", args.imports,
                       "--build-dir", str(tree / name), "--resource", str(resource)]
            result = subprocess.run(command, capture_output=True, text=True, check=False)
            if result.returncode != expected:
                print(f"{name}: expected {expected}, got {result.returncode}\n"
                      f"{result.stdout}{result.stderr}")
                return 1
            print(f"QML lint gate: {name} passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
