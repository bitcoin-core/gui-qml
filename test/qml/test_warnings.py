#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Verify warning failures and the Qt 6.2 compatibility path in real processes."""

import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def check_ctest_console(binary, platform, build_dir, enabled):
    # Even --show-only writes CTest's log files. Read a copy of the configured
    # test definitions so this child cannot replace the parent CTest's logs.
    root = Path(build_dir)
    definitions = list(root.rglob("CTestTestfile.cmake"))
    with tempfile.TemporaryDirectory(prefix="warning-policy-", dir=build_dir) as metadata:
        for definition in definitions:
            target = Path(metadata) / definition.relative_to(root)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(definition, target)
        configured = subprocess.run(
            ["ctest", "--test-dir", metadata, "--show-only=json-v1"],
            stdout=subprocess.PIPE, text=True, timeout=30, check=True,
        )
    test = next(test for test in json.loads(configured.stdout)["tests"] if test["name"] == "bitcoinqml_qmltests")
    properties = {prop["name"]: prop["value"] for prop in test["properties"]}
    # Use the actual configured policy so deleting either the CTest warning
    # check or its message formatting makes these process probes fail.
    for case, diagnostic in {
        "console_startup": "startup-console-warning-probe",
        "console_teardown": "teardown-console-warning-probe",
        "log_startup": "startup-console-log-probe",
    }.items():
        with tempfile.TemporaryDirectory(prefix="warning-probe-", dir=build_dir) as temporary:
            command = [binary, "-platform", platform, "-input", ":/test/qml/warning_probe.qml", f"WarningProbe::test_{case}"]
            definition = "add_test(probe " + " ".join(f"[==[{argument}]==]" for argument in command) + ")\n"
            for key in ("FAIL_REGULAR_EXPRESSION", "ENVIRONMENT"):
                if key in properties:
                    value = ";".join(properties[key])
                    definition += f"set_tests_properties(probe PROPERTIES {key} [==[{value}]==])\n"
            (Path(temporary) / "CTestTestfile.cmake").write_text(definition, encoding="utf8")
            result = subprocess.run(
                ["ctest", "--test-dir", temporary, "--output-on-failure"],
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=60, check=False,
            )
            expect_failure = enabled and case != "log_startup"
            assert (result.returncode > 0) == expect_failure, result.stdout
            if expect_failure:
                assert diagnostic in result.stdout, result.stdout
                assert "Error regular expression found in output" in result.stdout, result.stdout
            print(f"{case}: CTest exit={result.returncode}")


def main():
    binary, qt_version, platform, build_dir = sys.argv[1:]
    enabled = tuple(map(int, qt_version.split(".")[:2])) >= (6, 3)
    cases = {
        "clean": None,
        "console": "console-warning-probe",
        "binding": "missingBindingWarningProbe",
        "startup": "missingStartupWarningProbe",
        "teardown": "missingTeardownWarningProbe",
    }
    for case, diagnostic in cases.items():
        result = subprocess.run(
            [binary, "-platform", platform, "-input", ":/test/qml/warning_probe.qml", f"WarningProbe::test_{case}"],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=60, check=False,
        )
        assert "Totals:" in result.stdout, result.stdout
        if diagnostic is not None:
            assert diagnostic in result.stdout, result.stdout
        if diagnostic is not None and enabled:
            assert result.returncode > 0, result.stdout
        else:
            assert result.returncode == 0, result.stdout
        print(f"{case}: exit={result.returncode}, warning failures {'enabled' if enabled else 'disabled (Qt 6.2)'}")
    check_ctest_console(binary, platform, build_dir, enabled)


if __name__ == "__main__":
    main()
