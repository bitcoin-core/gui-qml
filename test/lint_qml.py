#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Lint QML at its resource paths, with generated C++ type descriptions."""

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import xml.etree.ElementTree as ET


def is_qt62_compatibility_note(diagnostic):
    """Keep migration advice that cannot be applied with our minimum Qt version."""
    if diagnostic["type"] != "info":
        return False
    if diagnostic["id"] == "Quick.property-changes-parsed":
        # The target-less syntax was added after Qt 6.2.
        return True
    return diagnostic["id"] == "unqualified" and [
        suggestion.get("replacement") for suggestion in diagnostic.get("suggestions", [])
    ] == ["pragma ComponentBehavior: Bound\n"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qmllint", type=Path, required=True)
    parser.add_argument("--imports", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--resource", type=Path, action="append", required=True)
    args = parser.parse_args()

    args.build_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="qmllint-", dir=args.build_dir) as temporary:
        tree = Path(temporary)
        originals = {}
        for resource_file in args.resource:
            for resource in ET.parse(resource_file).getroot().findall("qresource"):
                for entry in resource.findall("file"):
                    relative = Path(resource.get("prefix", "").lstrip("/")) / entry.get("alias", entry.text)
                    if relative.suffix not in {".qml", ".js"} and relative.name != "qmldir":
                        continue
                    original = (resource_file.parent / entry.text).resolve()
                    target = tree / relative
                    if target in originals:
                        raise ValueError(f"Duplicate resource path: {relative}")
                    target.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(original, target)
                    originals[target] = original

        files = sorted(str(path) for path in originals if path.suffix in {".qml", ".js"})
        if not files:
            raise ValueError("No QML or JavaScript resources to lint")
        report_path = args.build_dir / "qmllint.json"
        report_path.unlink(missing_ok=True)
        command = [
            str(args.qmllint), "--ignore-settings", "--max-warnings", "0", "--unused-imports", "warning",
            # Still diagnose these categories; the JSON check below rejects all
            # unqualified accesses except the Qt 6.4-only Bound pragma advice.
            "--unqualified", "info", "--Quick.property-changes-parsed", "info",
            "-I", str(args.imports), "--json", str(report_path), *files,
        ]
        result = subprocess.run(command, check=False, capture_output=True, text=True)
        if not report_path.is_file():
            print(result.stdout, end="")
            print(result.stderr, end="")
            print(f"qmllint did not produce {report_path}")
            return 1
        report = json.loads(report_path.read_text(encoding="utf-8"))
        failures = 0
        compatibility_notes = 0
        for entry in report["files"]:
            entry["filename"] = str(originals.get(Path(entry["filename"]), entry["filename"]))
            for diagnostic in entry["warnings"]:
                if is_qt62_compatibility_note(diagnostic):
                    diagnostic["qt62Compatibility"] = True
                    compatibility_notes += 1
                    continue
                if diagnostic["id"] == "unqualified":
                    diagnostic["type"] = "warning"
                failures += diagnostic["type"] in {"warning", "error", "critical"}
                print(f"{entry['filename']}:{diagnostic['line']}:{diagnostic['column']}: "
                      f"{diagnostic['type']}: {diagnostic['message']} [{diagnostic['id']}]")
        report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        if result.returncode != 0:
            # Include tool/plugin failures even if no JSON diagnostic explains them.
            for output in (result.stdout, result.stderr):
                for staged, original in originals.items():
                    output = output.replace(str(staged), str(original))
                print(output, end="")
        print(f"qmllint: {len(files)} files, {failures} warnings/errors, "
              f"{compatibility_notes} Qt 6.2 compatibility notes (retained in {report_path})")
        return int(result.returncode != 0 or failures != 0)


if __name__ == "__main__":
    raise SystemExit(main())
