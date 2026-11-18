#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Exercise the QML lint gate against actual valid and invalid resource trees."""

import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile

from qml_api_schema import check_schema


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qmllint", required=True)
    parser.add_argument("--imports", required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    args = parser.parse_args()
    args.build_dir.mkdir(parents=True, exist_ok=True)
    schema = check_schema(Path(__file__).resolve().parents[1], Path(args.imports).resolve().parent,
                          args.build_dir / "api-schema.json")
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
        "typed-editor-read-and-intent": (
            'import org.bitcoincore.qt 1.0\nimport "../controls"\n'
            'Base { id: root; property SettingsEditorModel editor: null; '
            'property bool saveEnabled: root.editor ? root.editor.canSave : false; '
            'property int originalPhase: root.editor ? root.editor.phase : SettingsEditorModel.Loading; '
            'function requestSave() { if (root.editor) root.editor.save() } }', 0),
        "typed-original-result-observation": (
            'import org.bitcoincore.qt 1.0\nimport "../controls"\n'
            'Base { id: root; property WalletOperationModel operation: null; '
            'property string originalId: root.operation ? root.operation.operationId : ""; '
            'property WalletQmlModel originalResult: root.operation ? root.operation.resultWallet : null; '
            'property bool canOpen: root.operation ? root.operation.canOpenResult : false }', 0),
        "typed-sharing-interest-and-intent": (
            'import org.bitcoincore.qt 1.0\nimport "../controls"\n'
            'Base { id: root; property RequestSharingModel sharing: null; '
            'property bool canCopy: root.sharing ? root.sharing.canCopyUri : false; '
            'function hideView() { if (root.sharing) root.sharing.active = false } '
            'function copy() { if (root.sharing) root.sharing.copyUri() } }', 0),
        "typed-selection-intent": (
            'import org.bitcoincore.qt 1.0\nimport "../controls"\n'
            'Base { id: root; property WalletSelectionModel selection: null; '
            'property WalletQmlModel original: root.selection ? root.selection.selectedWallet : null; '
            'function choose() { if (root.selection) root.selection.select(root.original) } }', 0),
        "reject-editor-state-writer": (
            'import org.bitcoincore.qt 1.0\nimport "../controls"\n'
            'Base { id: root; property SettingsEditorModel editor: null; '
            'function mutate() { if (root.editor) root.editor.phase = SettingsEditorModel.Saving } }', 1, "phase"),
        "reject-result-authority-writer": (
            'import org.bitcoincore.qt 1.0\nimport "../controls"\n'
            'Base { id: root; property WalletOperationModel operation: null; '
            'function mutate() { if (root.operation) root.operation.outcomeKnown = true } }', 1, "outcomeKnown"),
        "reject-unowned-backend-query": (
            'import org.bitcoincore.qt 1.0\nimport "../controls"\n'
            'Base { id: root; property SettingsEditorModel editor: null; '
            'function query() { if (root.editor) root.editor.walletBackend.getName() } }', 1, "walletBackend"),
        "reject-generic-state-setter": (
            'import org.bitcoincore.qt 1.0\nimport "../controls"\n'
            'Base { id: root; property SettingsEditorModel editor: null; '
            'function mutate() { if (root.editor) root.editor.setPhase(SettingsEditorModel.Saving) } }', 1, "setPhase"),
        "reject-sharing-phase-writer": (
            'import org.bitcoincore.qt 1.0\nimport "../controls"\n'
            'Base { id: root; property RequestSharingModel sharing: null; '
            'function mutate() { if (root.sharing) root.sharing.phase = RequestSharingModel.Closed } }', 1, "phase"),
        "typed-node-observation-and-intent": (
            'import org.bitcoincore.qt 1.0\nimport "../controls"\n'
            'Base { id: root; property NodeModel node: null; '
            'property bool ready: root.node ? root.node.networkActionReady : false; '
            'function stop() { if (root.node) root.node.requestShutdown() } }', 0),
        "typed-peer-owned-observation": (
            'import org.bitcoincore.qt 1.0\nimport "../controls"\n'
            'Base { id: root; property PeerSelectionModel selection: null; '
            'property PeerDetailsModel details: root.selection ? root.selection.selectedDetails : null; '
            'property bool connected: root.details ? root.details.connected : false }', 0),
        "reject-request-ticket-property": (
            'import "../controls"\nBase { property double sendTicket: 0 }', 1, "sendTicket"),
        "reject-request-token-property": (
            'import "../controls"\nBase { property string pendingToken: "" }', 1, "pendingToken"),
        "readonly-model-token-binding": (
            'import "../controls"\nBase { readonly property string questionToken: "" }', 0),
        "reject-node-lifecycle-writer": (
            'import org.bitcoincore.qt 1.0\nimport "../controls"\n'
            'Base { id: root; property NodeModel node: null; '
            'function mutate() { if (root.node) root.node.lifecyclePhase = 0 } }', 1, "lifecyclePhase"),
    }
    wallet_cases = {"typed-original-result-observation", "typed-sharing-interest-and-intent", "typed-selection-intent",
                    "reject-result-authority-writer", "reject-sharing-phase-writer"}
    inapplicable = []
    if not schema["configured_wallet"]:
        inapplicable = sorted(wallet_cases)
        cases = {name: case for name, case in cases.items() if name not in wallet_cases}
        # This is the explicitly conditional foreign type. Other historical
        # wallet foreign names are not assumed to disappear in this variant.
        cases["wallet-off-import-type-unavailable"] = (
            'import org.bitcoincore.qt 1.0\nimport "../controls"\n'
            'Base { property PaymentImportOperation unavailable: null }', 1, "PaymentImportOperation")
    (args.build_dir / "configured-controls.json").write_text(json.dumps({
        "configured_wallet": schema["configured_wallet"], "selected": list(cases),
        "variant_inapplicable": inapplicable, "count": len(cases)}, indent=2) + "\n", encoding="utf-8")
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
        for name, case in cases.items():
            source, expected = case[:2]
            (tree / "use.qml").write_text(source + "\n", encoding="utf-8")
            command = [sys.executable, str(Path(__file__).with_name("lint_qml.py")),
                       "--qmllint", args.qmllint, "--imports", args.imports,
                       "--build-dir", str(tree / name), "--resource", str(resource)]
            result = subprocess.run(command, capture_output=True, text=True, check=False)
            report_file = tree / name / "qmllint.json"
            report = json.loads(report_file.read_text(encoding="utf-8")) if report_file.is_file() else None
            diagnostics = [] if report is None else [warning for entry in report["files"] for warning in entry["warnings"]
                                                    if warning["type"] in {"warning", "error", "critical"}]
            retained = args.build_dir / "controls" / name
            retained.mkdir(parents=True, exist_ok=True)
            (retained / "stdout").write_text(result.stdout, encoding="utf-8")
            (retained / "stderr").write_text(result.stderr, encoding="utf-8")
            (retained / "result.json").write_text(json.dumps({"source": source, "expected_exit": expected,
                "actual_exit": result.returncode, "command": command, "diagnostics": diagnostics}, indent=2) + "\n", encoding="utf-8")
            if result.returncode != expected:
                print(f"{name}: expected {expected}, got {result.returncode}\n"
                      f"{result.stdout}{result.stderr}")
                return 1
            if len(case) == 3 and not any(case[2].lower() in diagnostic["message"].lower() for diagnostic in diagnostics):
                print(f"{name}: failure did not diagnose the actual model member {case[2]!r}\n{result.stdout}{result.stderr}")
                return 1
            print(f"QML lint gate: {name} passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
