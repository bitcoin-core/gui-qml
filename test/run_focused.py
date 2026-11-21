#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Run explicit selections from already-built GUI-QML test binaries."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import re
import signal
import subprocess
import sys
import time
import xml.etree.ElementTree as ET
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


VERSION = 1
DEFAULT_TIMEOUT_SECONDS = 180.0
INFRASTRUCTURE_FUNCTIONS = {"initTestCase", "cleanupTestCase", "init", "cleanup"}
REPORTING_ARGS = {
    "-o", "-txt", "-csv", "-xml", "-lightxml", "-junitxml", "-xunitxml",
    "-teamcity", "-tap", "--test-discover", "--list-functions", "--list-modules",
    "--test-prepare-funded-wallet", "--list-test-classes", "--help", "-help", "-functions", "-datatags",
    "--output", "--report", "--report-file",
}
UNSUPPORTED_CTEST_POLICIES = {
    "PASS_REGULAR_EXPRESSION", "SKIP_REGULAR_EXPRESSION", "SKIP_RETURN_CODE", "WILL_FAIL",
    "ENVIRONMENT_MODIFICATION", "FIXTURES_REQUIRED", "FIXTURES_SETUP", "FIXTURES_CLEANUP",
    "DEPENDS", "RESOURCE_LOCK", "RESOURCE_GROUPS", "PROCESSORS", "RUN_SERIAL", "DISABLED",
}


class RunnerError(Exception):
    pass


def _is_nonempty_string(value: Any) -> bool:
    return isinstance(value, str) and bool(value.strip())


def _selector_function_row(selector: str, suite: str) -> tuple[str, str | None]:
    if suite == "qml":
        separator = selector.find(":", selector.find("::") + 2)
    else:
        separator = selector.find(":")
    if separator < 0:
        return selector, None
    function, row = selector[:separator], selector[separator + 1:]
    if not function or not row or ":" in row:
        raise RunnerError(f"invalid test selector {selector!r}; expected function[:row]")
    return function, row


def _validate_selectors(values: Any, suite: str, required: bool) -> list[str]:
    if values is None and not required:
        return []
    if not isinstance(values, list) or not values:
        raise RunnerError(f"{suite} tests must be a nonempty list when supplied")
    selectors: list[str] = []
    parsed: list[tuple[str, str | None]] = []
    for value in values:
        if not _is_nonempty_string(value):
            raise RunnerError(f"{suite} test selectors must be nonempty strings")
        selector = value.strip()
        if any(char in selector for char in "\r\n\0") or "[" in selector or "]" in selector:
            raise RunnerError(f"invalid test selector {selector!r}")
        function, row = _selector_function_row(selector, suite)
        if suite == "unit" and ("::" in function or not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", function)):
            raise RunnerError(f"unit selector must name a test function: {selector!r}")
        if suite == "qml" and ("::" not in function or function.startswith("::") or function.endswith("::")):
            raise RunnerError(f"QML selector must use TestCase::function: {selector!r}")
        if suite == "qml" and not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*::[A-Za-z_][A-Za-z0-9_]*", function):
            raise RunnerError(f"invalid QML selector {selector!r}")
        if selector in selectors:
            raise RunnerError(f"duplicate test selector {selector!r}")
        if any(previous_function == function and (previous_row is None or row is None) for previous_function, previous_row in parsed):
            raise RunnerError(f"overlapping selectors would duplicate test bodies for {function!r}")
        selectors.append(selector)
        parsed.append((function, row))
    return selectors


def _validate_integration_args(args: Any) -> list[str]:
    if not isinstance(args, list) or not args:
        raise RunnerError("integration args must be a nonempty list")
    result: list[str] = []
    for arg in args:
        if not _is_nonempty_string(arg) or "\0" in arg or "\n" in arg or "\r" in arg:
            raise RunnerError("integration args must be nonempty single-line strings")
        value = arg.strip()
        if value in REPORTING_ARGS or value.startswith(("--test-discover=", "--list-functions=", "--list-modules=")):
            raise RunnerError(f"integration arg {value!r} is reserved for reporting or discovery")
        if value.startswith(("--output=", "--report=", "--report-file=")):
            raise RunnerError(f"integration arg {value!r} is reserved for reporting")
        result.append(value)
    if sum(arg.startswith("--test=") for arg in result) > 1:
        raise RunnerError("integration selection must contain at most one --test selector")
    return result


def validate_manifest(document: Any) -> list[dict[str, Any]]:
    if not isinstance(document, dict) or set(document) != {"version", "tests"}:
        raise RunnerError("manifest must contain exactly 'version' and 'tests'")
    if (isinstance(document["version"], bool) or not isinstance(document["version"], int) or
            document["version"] != VERSION):
        raise RunnerError(f"unsupported manifest version {document['version']!r}")
    entries = document["tests"]
    if not isinstance(entries, list) or not entries:
        raise RunnerError("manifest tests must be a nonempty list")

    selections: list[dict[str, Any]] = []
    ids: set[str] = set()
    for index, raw in enumerate(entries):
        if not isinstance(raw, dict):
            raise RunnerError(f"tests[{index}] must be an object")
        suite = raw.get("suite")
        required_by_suite = {
            "unit": {"id", "suite", "class"},
            "qml": {"id", "suite", "file"},
            "integration": {"id", "suite", "args"},
        }
        if suite not in required_by_suite:
            raise RunnerError(f"tests[{index}] has unknown suite {suite!r}")
        allowed = required_by_suite[suite] | {"tests", "expected"}
        if set(raw) - allowed:
            raise RunnerError(f"tests[{index}] has unknown fields: {sorted(set(raw) - allowed)}")
        missing = required_by_suite[suite] - set(raw)
        if missing:
            raise RunnerError(f"tests[{index}] is missing fields: {sorted(missing)}")

        selection_id = raw["id"]
        if not _is_nonempty_string(selection_id) or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]*", selection_id):
            raise RunnerError(f"tests[{index}].id must start with a letter or digit and use only letters, digits, '.', '_' or '-'")
        if selection_id in ids:
            raise RunnerError(f"duplicate selection id {selection_id!r}")
        ids.add(selection_id)

        if "expected" in raw and raw["expected"] is None:
            raise RunnerError(f"tests[{index}].expected must be omitted or a positive integer")
        expected = raw.get("expected")
        if expected is not None and (isinstance(expected, bool) or not isinstance(expected, int) or expected < 1):
            raise RunnerError(f"tests[{index}].expected must be a positive integer")

        selection: dict[str, Any] = {"id": selection_id, "suite": suite, "expected": expected}
        if suite == "unit":
            if not _is_nonempty_string(raw["class"]) or not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", raw["class"]):
                raise RunnerError(f"tests[{index}].class must be a C++ test class name")
            selection["class"] = raw["class"]
            if "tests" in raw and raw["tests"] is None:
                raise RunnerError(f"tests[{index}].tests must be omitted or a nonempty list")
            selection["tests"] = _validate_selectors(raw.get("tests"), suite, required=False)
        elif suite == "qml":
            filename = raw["file"]
            if not _is_nonempty_string(filename) or Path(filename).name != filename or not re.fullmatch(r"tst_[A-Za-z0-9_-]+\.qml", filename):
                raise RunnerError(f"tests[{index}].file must be a QML test basename such as tst_example.qml")
            selection["file"] = filename
            if "tests" in raw and raw["tests"] is None:
                raise RunnerError(f"tests[{index}].tests must be omitted or a nonempty list")
            selection["tests"] = _validate_selectors(raw.get("tests"), suite, required=False)
        else:
            if "tests" in raw:
                raise RunnerError("integration selections use args, not a separate tests field")
            selection["args"] = _validate_integration_args(raw["args"])
        selections.append(selection)
    return selections


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _write_bytes(path: Path, data: bytes) -> dict[str, Any]:
    path.write_bytes(data)
    return {"path": path.name, "bytes": len(data), "sha256": _sha256(data)}


def _run_process(command: list[str], cwd: Path, env_overrides: dict[str, str], timeout: float,
                 report_dir: Path, prefix: str) -> dict[str, Any]:
    env = os.environ.copy()
    env.update(env_overrides)
    started = time.monotonic()
    result: dict[str, Any] = {
        "command": command,
        "cwd": str(cwd),
        "environment_overrides": env_overrides,
        "timeout_seconds": timeout,
        "return_code": None,
        "timed_out": False,
        "duration_seconds": None,
    }
    try:
        with subprocess.Popen(command, cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              start_new_session=os.name == "posix") as process:
            try:
                stdout, stderr = process.communicate(timeout=timeout)
                result["return_code"] = process.returncode
            except subprocess.TimeoutExpired:
                # Integration tests own child application processes. Terminate
                # our process group on Unix so a watchdog cannot leave them
                # running or holding the captured output pipes open.
                if os.name == "posix":
                    try:
                        os.killpg(process.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                else:
                    process.kill()
                stdout, stderr = process.communicate()
                result["timed_out"] = True
    except OSError as error:
        stdout = b""
        stderr = str(error).encode("utf-8", errors="replace")
        result["launch_error"] = str(error)
    result["duration_seconds"] = round(time.monotonic() - started, 6)
    result["stdout"] = _write_bytes(report_dir / f"{prefix}.stdout.raw", stdout)
    result["stderr"] = _write_bytes(report_dir / f"{prefix}.stderr.raw", stderr)
    return result


def compose_command(base: list[str], selection: dict[str, Any], xml_path: Path, text_path: Path) -> list[str]:
    suite = selection["suite"]
    if suite == "unit":
        command = base + [f"--test-class={selection['class']}"] + selection["tests"]
    elif suite == "qml":
        command = base + selection["tests"]
    else:
        command = base + selection["args"]
    return command + ["-o", f"{xml_path.resolve()},xml", "-o", f"{text_path.resolve()},txt"]


def process_failure_issues(process: dict[str, Any]) -> list[str]:
    if process["return_code"] != 0 or process["timed_out"] or "launch_error" in process:
        return ["test process timed out, could not launch, or returned a nonzero status"]
    return []


def matching_fail_expressions(patterns: list[tuple[str, re.Pattern[str]]], output: str) -> list[str]:
    return [pattern for pattern, compiled in patterns if compiled.search(output)]


def captured_output_text(entry_dir: Path, process: dict[str, Any], text_path: Path) -> str:
    chunks = [(entry_dir / process[key]["path"]).read_bytes() for key in ("stdout", "stderr")]
    if text_path.is_file():
        chunks.append(text_path.read_bytes())
    return b"\n".join(chunks).decode("utf-8", errors="replace")


def _parse_ctest_entries(raw: bytes) -> dict[str, dict[str, Any]]:
    try:
        document = json.loads(raw)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise RunnerError(f"CTest JSON is malformed: {error}") from error
    if not isinstance(document, dict) or not isinstance(document.get("tests"), list):
        raise RunnerError("CTest JSON has no tests array")
    result: dict[str, dict[str, Any]] = {}
    for entry in document["tests"]:
        if not isinstance(entry, dict) or not _is_nonempty_string(entry.get("name")):
            raise RunnerError("CTest JSON contains a malformed test entry")
        if entry["name"] in result:
            raise RunnerError(f"CTest JSON contains duplicate test name {entry['name']!r}")
        command = entry.get("command")
        command_error = None
        if command is None or command == []:
            command_error = "CTest entry has no command; its target may not be built"
            command = None
        elif not isinstance(command, list) or not command or not all(isinstance(arg, str) and arg for arg in command):
            command_error = "CTest entry has a malformed command"
            command = None
        props: dict[str, Any] = {}
        for prop in entry.get("properties", []):
            if isinstance(prop, dict) and isinstance(prop.get("name"), str):
                props[prop["name"]] = prop.get("value")
        result[entry["name"]] = {"name": entry["name"], "command": command,
                                  "command_error": command_error, "properties": props}
    return result


def _property_env(properties: dict[str, Any]) -> dict[str, str]:
    raw = properties.get("ENVIRONMENT", [])
    if raw is None:
        return {}
    values = raw if isinstance(raw, list) else [raw]
    overrides: dict[str, str] = {}
    for item in values:
        if not isinstance(item, str) or "=" not in item:
            raise RunnerError(f"unsupported CTest ENVIRONMENT entry: {item!r}")
        key, value = item.split("=", 1)
        if not key:
            raise RunnerError("CTest ENVIRONMENT contains an empty variable name")
        overrides[key] = value
    return overrides


def _entry_properties(entry: dict[str, Any], default_cwd: Path, cli_timeout: float) -> dict[str, Any]:
    props = entry["properties"]
    unsupported = [key for key in UNSUPPORTED_CTEST_POLICIES if key in props and props[key] not in (None, False, 0, "", [])]
    if unsupported:
        raise RunnerError(f"unsupported CTest execution policy on {entry['name']}: {sorted(unsupported)}")
    cwd_value = props.get("WORKING_DIRECTORY")
    if isinstance(cwd_value, str) and cwd_value:
        cwd_path = Path(cwd_value)
        cwd = (cwd_path if cwd_path.is_absolute() else default_cwd / cwd_path).resolve()
    else:
        cwd = default_cwd
    if not cwd.is_dir():
        raise RunnerError(f"CTest working directory does not exist: {cwd}")
    timeout = cli_timeout
    ctest_timeout = props.get("TIMEOUT")
    if ctest_timeout not in (None, ""):
        try:
            parsed_timeout = float(ctest_timeout)
        except (TypeError, ValueError) as error:
            raise RunnerError(f"invalid CTest TIMEOUT on {entry['name']}: {ctest_timeout!r}") from error
        if not math.isfinite(parsed_timeout) or parsed_timeout <= 0:
            raise RunnerError(f"invalid CTest TIMEOUT on {entry['name']}: {ctest_timeout!r}")
        timeout = min(timeout, parsed_timeout)

    raw_fail = props.get("FAIL_REGULAR_EXPRESSION", [])
    patterns = raw_fail if isinstance(raw_fail, list) else ([raw_fail] if raw_fail else [])
    if len(patterns) == 1 and isinstance(patterns[0], str) and ";" in patterns[0]:
        patterns = patterns[0].split(";")
    compiled: list[tuple[str, re.Pattern[str]]] = []
    for pattern in patterns:
        if not isinstance(pattern, str):
            raise RunnerError(f"invalid CTest FAIL_REGULAR_EXPRESSION on {entry['name']}")
        try:
            compiled.append((pattern, re.compile(pattern)))
        except re.error as error:
            raise RunnerError(f"CTest fail expression cannot be honored for {entry['name']}: {error}") from error
    return {"cwd": cwd, "timeout": timeout, "env": _property_env(props), "fail_patterns": compiled}


def _ctest_input_file(entry: dict[str, Any]) -> str | None:
    command = entry["command"]
    try:
        index = command.index("-input")
    except ValueError:
        return None
    if index + 1 >= len(command):
        return None
    value = command[index + 1].replace("\\", "/")
    return value.rsplit("/", 1)[-1]


def _find_qml_ctest(entries: dict[str, dict[str, Any]], filename: str) -> dict[str, Any]:
    matches = [entry for entry in entries.values() if entry["command"] and _ctest_input_file(entry) == filename]
    if len(matches) != 1:
        suffix = filename.removeprefix("tst_").removesuffix(".qml")
        missing_target = entries.get(f"bitcoinqml_qml_{suffix}")
        if not matches and missing_target and not missing_target["command"]:
            raise RunnerError(f"selected QML CTest target {missing_target['name']!r} has no command; target may not be built")
        raise RunnerError(f"CTest discovery found {len(matches)} registered commands for {filename!r}")
    return matches[0]


def _test_functions(entry: ET.Element, suite: str) -> list[dict[str, Any]]:
    identities: list[dict[str, Any]] = []
    for function_node in entry.findall("TestFunction"):
        function = function_node.get("name")
        if not function:
            raise RunnerError("Qt Test XML contains a TestFunction without a name")
        short_function = function.rsplit("::", 1)[-1]
        incidents = function_node.findall("Incident")
        kinds_by_row: dict[str, list[str]] = {}
        for incident in incidents:
            tags = [tag.text or "" for tag in incident.findall("DataTag")]
            row = tags[0] if tags else ""
            if len(set(tags)) > 1:
                raise RunnerError(f"Qt Test XML has conflicting data tags in {function!r}")
            kinds_by_row.setdefault(row, []).append(incident.get("type", "").lower())
        message_kinds_by_row: dict[str, list[str]] = {}
        for message in function_node.findall("Message"):
            tags = [tag.text or "" for tag in message.findall("DataTag")]
            if len(set(tags)) > 1:
                raise RunnerError(f"Qt Test XML has conflicting message data tags in {function!r}")
            message_kinds_by_row.setdefault(tags[0] if tags else "", []).append(message.get("type", "").lower())
        if not kinds_by_row and not message_kinds_by_row:
            kinds_by_row[""] = []
        rows = list(kinds_by_row)
        rows.extend(row for row in message_kinds_by_row if row not in kinds_by_row)
        for row in rows:
            kinds = kinds_by_row.get(row, [])
            message_kinds = message_kinds_by_row.get(row, [])
            # Messages with DataTags affect only their own data row.
            failed = any(kind in {"fail", "fatal", "xpass"} for kind in kinds)
            skipped = (any(kind in {"skip", "skipped"} for kind in kinds) or
                       any("skip" in kind for kind in message_kinds))
            passed = any(kind in {"pass", "xfail"} for kind in kinds)
            if failed:
                status = "fail"
            elif skipped:
                status = "skip"
            elif passed:
                status = "pass"
            else:
                status = "fail"
            identity = row if suite == "integration" and row else function
            if suite != "integration" and row:
                identity = f"{function}:{row}"
            infrastructure = short_function in INFRASTRUCTURE_FUNCTIONS
            identities.append({"identity": identity, "function": function, "row": row or None,
                               "status": status, "infrastructure": infrastructure})
    return identities


def _parse_result_xml(path: Path, suite: str, expected_class: str | None) -> dict[str, Any]:
    if not path.is_file() or path.stat().st_size == 0:
        raise RunnerError("test process did not produce a nonempty XML report")
    raw = path.read_bytes()
    try:
        root = ET.fromstring(raw)
    except ET.ParseError as error:
        raise RunnerError(f"test XML is malformed: {error}") from error
    if root.tag != "TestCase":
        raise RunnerError(f"unexpected Qt Test XML root element {root.tag!r}")
    if suite == "unit" and expected_class and root.get("name") != expected_class:
        raise RunnerError(f"unit XML class is {root.get('name')!r}, expected {expected_class!r}")
    bodies = _test_functions(root, suite)
    identities = [body["identity"] for body in bodies if not body["infrastructure"]]
    duplicate_ids = sorted({identity for identity in identities if identities.count(identity) > 1})
    if duplicate_ids:
        raise RunnerError(f"duplicate selected body identities: {duplicate_ids}")
    infrastructure_failures = [body for body in bodies if body["infrastructure"] and body["status"] == "fail"]
    infrastructure_skips = [body for body in bodies if body["infrastructure"] and body["status"] == "skip"]
    body_results = [body for body in bodies if not body["infrastructure"]]
    return {
        "xml_sha256": _sha256(raw),
        "identities": identities,
        "body_count": len(body_results),
        "passed": sum(body["status"] == "pass" for body in body_results),
        "failed": sum(body["status"] == "fail" for body in body_results),
        "skipped": sum(body["status"] == "skip" for body in body_results),
        "infrastructure_failures": len(infrastructure_failures),
        "infrastructure_failure_functions": [body["function"] for body in infrastructure_failures],
        "infrastructure_skips": len(infrastructure_skips),
        "infrastructure_skip_functions": [body["function"] for body in infrastructure_skips],
        "bodies": body_results,
    }


def _validate_selected_identities(selection: dict[str, Any], parsed: dict[str, Any]) -> list[str]:
    if selection["suite"] in ("unit", "qml"):
        selectors = selection["tests"]
        if not selectors:
            return []
        allowed: list[tuple[str, str | None]] = [_selector_function_row(selector, selection["suite"]) for selector in selectors]
        unexpected: list[str] = []
        for body in parsed["bodies"]:
            function = body["function"]
            row = body["row"]
            if selection["suite"] == "unit":
                function = function.rsplit("::", 1)[-1]
            if not any(function == wanted and (wanted_row is None or wanted_row == row) for wanted, wanted_row in allowed):
                unexpected.append(body["identity"])
        missing = []
        for wanted, wanted_row in allowed:
            if wanted_row is None:
                if not any((body["function"].rsplit("::", 1)[-1] if selection["suite"] == "unit" else body["function"]) == wanted
                           for body in parsed["bodies"]):
                    missing.append(wanted)
            elif not any((body["function"].rsplit("::", 1)[-1] if selection["suite"] == "unit" else body["function"]) == wanted and body["row"] == wanted_row
                         for body in parsed["bodies"]):
                missing.append(f"{wanted}:{wanted_row}")
        issues = []
        if unexpected:
            issues.append(f"unexpected selected body identities: {unexpected}")
        if missing:
            issues.append(f"requested selectors produced no body: {missing}")
        return issues

    args = selection["args"]
    module = next((arg.split("=", 1)[1] for arg in args if arg.startswith("--module=")), None)
    wallet = next((arg.split("=", 1)[1] for arg in args if arg.startswith("--wallet=")), None)
    test_selector = next((arg.split("=", 1)[1] for arg in args if arg.startswith("--test=")), None)
    issues: list[str] = []
    unexpected: list[str] = []
    for body in parsed["bodies"]:
        identity = body["identity"]
        parts = identity.split(".", 2)
        if len(parts) != 3:
            unexpected.append(identity)
            continue
        actual_module, actual_wallet, actual_test = parts
        if module is not None and actual_module != module:
            unexpected.append(identity)
        elif wallet is not None and actual_wallet != wallet:
            unexpected.append(identity)
        elif test_selector is not None:
            wanted_function, wanted_row = _selector_function_row(test_selector, "unit")
            actual_function, actual_row = _selector_function_row(actual_test, "unit")
            if actual_function != wanted_function or (wanted_row is not None and actual_row != wanted_row):
                unexpected.append(identity)
    if unexpected:
        issues.append(f"unexpected integration body identities: {unexpected}")
    if test_selector is not None:
        wanted_function, wanted_row = _selector_function_row(test_selector, "unit")
        matched = False
        for body in parsed["bodies"]:
            parts = body["identity"].split(".", 2)
            if len(parts) != 3:
                continue
            actual_function, actual_row = _selector_function_row(parts[2], "unit")
            if actual_function == wanted_function and (wanted_row is None or actual_row == wanted_row):
                matched = True
                break
        if not matched:
            issues.append(f"requested integration selector produced no body: {test_selector}")
    return issues


def _run_selection(selection: dict[str, Any], build_dir: Path, out_dir: Path,
                    ctest: dict[str, dict[str, Any]] | None, unit_classes: set[str] | None,
                    cli_timeout: float) -> dict[str, Any]:
    entry_dir = out_dir / selection["id"]
    entry_dir.mkdir()
    suite = selection["suite"]
    result: dict[str, Any] = {"id": selection["id"], "suite": suite, "expected": selection["expected"],
                              "status": "fail", "issues": [], "process": None, "bodies": None}
    ctest_entry: dict[str, Any] | None = None
    base: list[str] = []
    props = {"cwd": build_dir, "timeout": cli_timeout, "env": {}, "fail_patterns": []}
    if suite == "unit":
        if ctest is None:
            result["issues"].append("CTest test discovery failed; cannot resolve registered unit command/environment")
        else:
            ctest_entry = ctest.get("bitcoinqml_unit_tests")
            if not ctest_entry:
                result["issues"].append("CTest has no bitcoinqml_unit_tests entry")
            elif not ctest_entry["command"]:
                result["issues"].append(ctest_entry["command_error"])
            elif unit_classes is None:
                result["issues"].append("unit class discovery failed")
            elif selection["class"] not in unit_classes:
                result["issues"].append(f"unit class {selection['class']!r} is absent from --list-test-classes")
        if ctest_entry and ctest_entry["command"]:
            try:
                props = _entry_properties(ctest_entry, build_dir, cli_timeout)
                base = list(ctest_entry["command"])
            except RunnerError as error:
                result["issues"].append(str(error))
    elif suite == "qml":
        if ctest is None:
            result["issues"].append("CTest test discovery failed; cannot resolve registered QML command/environment")
        else:
            try:
                ctest_entry = _find_qml_ctest(ctest, selection["file"])
                props = _entry_properties(ctest_entry, build_dir, cli_timeout)
                base = list(ctest_entry["command"])
            except RunnerError as error:
                result["issues"].append(str(error))
    else:
        if ctest is None:
            result["issues"].append("CTest discovery failed; cannot resolve integration command or registered environment")
        else:
            matches = [entry for entry in ctest.values()
                       if entry["command"] and Path(entry["command"][0]).name in {"bitcoinqml_integration_tests", "bitcoinqml_integration_tests.exe"}]
            if not matches:
                result["issues"].append("CTest has no registered integration command; refusing to bypass its policies")
            else:
                ctest_entry = matches[0]
                try:
                    props = _entry_properties(ctest_entry, build_dir, cli_timeout)
                    base = [ctest_entry["command"][0]]
                    if not any(arg.startswith("--profile-dir=") for arg in selection["args"]):
                        base.extend(arg for arg in ctest_entry["command"][1:] if arg.startswith("--profile-dir="))
                    for other in matches[1:]:
                        other_props = _entry_properties(other, build_dir, cli_timeout)
                        if (other["command"][0] != base[0] or other_props["cwd"] != props["cwd"] or
                                other_props["timeout"] != props["timeout"] or other_props["env"] != props["env"] or
                                [pattern for pattern, _ in other_props["fail_patterns"]] !=
                                [pattern for pattern, _ in props["fail_patterns"]]):
                            raise RunnerError("registered integration commands have differing execution policies")
                except RunnerError as error:
                    result["issues"].append(str(error))

    if result["issues"]:
        result["status"] = "fail"
        return result

    cwd = props["cwd"]
    env_overrides = dict(props["env"])
    if suite == "unit":
        # The manifest class is authoritative even if the invoking shell has
        # an inherited legacy filter. Record this override with the command.
        env_overrides["BITCOIN_QML_TEST_CLASSES"] = ""
    xml_path = (entry_dir / "results.xml").resolve()
    text_path = (entry_dir / "results.txt").resolve()
    command = compose_command(base, selection, xml_path, text_path)
    process = _run_process(command, cwd, env_overrides, props["timeout"], entry_dir, "run")
    result["process"] = process
    result["ctest_test"] = ctest_entry["name"] if ctest_entry else None
    if suite == "integration" and ctest is not None:
        result["ctest_test_candidates"] = [entry["name"] for entry in ctest.values()
                                           if entry["command"] and Path(entry["command"][0]).name in {"bitcoinqml_integration_tests", "bitcoinqml_integration_tests.exe"}]
    result["ctest_fail_regular_expressions"] = [pattern for pattern, _compiled in props["fail_patterns"]]
    result["issues"].extend(process_failure_issues(process))

    decoded_output = captured_output_text(entry_dir, process, text_path)
    result["issues"].extend(f"CTest FAIL_REGULAR_EXPRESSION matched: {pattern}"
                            for pattern in matching_fail_expressions(props["fail_patterns"], decoded_output))

    try:
        parsed = _parse_result_xml(xml_path, suite, selection.get("class"))
        result["bodies"] = parsed
        result["xml"] = {"path": xml_path.name, "bytes": xml_path.stat().st_size, "sha256": parsed["xml_sha256"]}
        result["issues"].extend(_validate_selected_identities(selection, parsed))
        if not parsed["body_count"]:
            result["issues"].append("test selection produced no test bodies")
        if parsed["infrastructure_failures"]:
            result["issues"].append(f"{parsed['infrastructure_failures']} setup/cleanup function(s) failed")
        if parsed["failed"]:
            result["issues"].append(f"{parsed['failed']} selected body/bodies failed")
        if selection["expected"] is not None and parsed["body_count"] != selection["expected"]:
            result["issues"].append(f"selected body count {parsed['body_count']} differs from expected {selection['expected']}")
    except RunnerError as error:
        result["issues"].append(str(error))

    result["text_report"] = ({"path": text_path.name, "bytes": text_path.stat().st_size,
                               "sha256": _sha256(text_path.read_bytes())} if text_path.is_file() else None)
    if result["issues"]:
        result["status"] = "fail"
    elif result["bodies"]["skipped"] or result["bodies"]["infrastructure_skips"]:
        result["status"] = "incomplete"
        result["issues"].append(
            f"{result['bodies']['skipped']} selected body/bodies and "
            f"{result['bodies']['infrastructure_skips']} setup/cleanup function(s) skipped")
    else:
        result["status"] = "pass"
    return result


def _run_ctest_discovery(build_dir: Path, discovery_dir: Path, timeout: float) -> tuple[dict[str, dict[str, Any]] | None, dict[str, Any]]:
    discovery_dir.mkdir()
    record = _run_process(["ctest", "--test-dir", str(build_dir), "--show-only=json-v1"], build_dir, {}, timeout,
                          discovery_dir, "ctest-discovery")
    raw = (discovery_dir / record["stdout"]["path"]).read_bytes()
    record["status"] = "fail"
    if record["return_code"] != 0 or record["timed_out"] or "launch_error" in record:
        record["issue"] = "CTest JSON discovery command failed"
        return None, record
    try:
        entries = _parse_ctest_entries(raw)
    except RunnerError as error:
        record["issue"] = str(error)
        return None, record
    record["status"] = "pass"
    record["test_count"] = len(entries)
    return entries, record


def _run_unit_class_discovery(entry: dict[str, Any] | None, build_dir: Path, discovery_dir: Path,
                              timeout: float) -> tuple[set[str] | None, dict[str, Any]]:
    discovery_dir.mkdir()
    if not entry:
        return None, {"status": "fail", "issue": "CTest has no registered unit executable"}
    if not entry["command"]:
        return None, {"status": "fail", "issue": entry["command_error"]}
    try:
        props = _entry_properties(entry, build_dir, timeout)
    except RunnerError as error:
        return None, {"status": "fail", "issue": str(error)}
    command = [entry["command"][0], "--list-test-classes"]
    env_overrides = dict(props["env"])
    env_overrides["BITCOIN_QML_TEST_CLASSES"] = ""
    process = _run_process(command, props["cwd"], env_overrides, props["timeout"], discovery_dir, "unit-classes")
    if process["return_code"] != 0 or process["timed_out"] or "launch_error" in process:
        return None, {"status": "fail", "issue": "unit class discovery failed", "process": process}
    raw = (discovery_dir / process["stdout"]["path"]).read_bytes().decode("utf-8", errors="replace")
    classes = {line.strip() for line in raw.splitlines() if line.strip()}
    if not classes or any(not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name) for name in classes):
        return None, {"status": "fail", "issue": "unit class discovery returned empty or malformed output", "process": process}
    return classes, {"status": "pass", "classes": sorted(classes), "count": len(classes), "process": process}


def _parse_args(argv: list[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT_SECONDS,
                        help=f"per-process watchdog in seconds (default: {DEFAULT_TIMEOUT_SECONDS:g})")
    args = parser.parse_args(argv)
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("--timeout must be finite and positive")
    return args


def unit_build_scope(build_dir: Path) -> dict[str, Any]:
    path = build_dir / "test" / "unit-build-selection.json"
    if not path.is_file():
        return {"mode": "unrecorded"}
    try:
        raw = path.read_bytes()
        scope = json.loads(raw)
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise RunnerError(f"invalid unit build scope: {error}") from error
    if not isinstance(scope, dict) or scope.get("mode") not in {"complete", "focused"}:
        raise RunnerError("invalid unit build scope mode")
    for key in ("requested_classes", "compiled_classes"):
        names = scope.get(key, [])
        if not isinstance(names, list) or any(not isinstance(name, str) or not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name) for name in names):
            raise RunnerError(f"invalid unit build scope {key}")
    if scope["mode"] == "focused" and (not scope.get("requested_classes") or
            not set(scope["requested_classes"]) <= set(scope.get("compiled_classes", []))):
        raise RunnerError("focused unit build scope has no complete class selection")
    return {**scope, "path": str(path), "sha256": _sha256(raw)}


def main(argv: list[str] | None = None) -> int:
    args = _parse_args(argv)
    build_dir = args.build_dir.resolve()
    manifest_path = args.manifest.resolve()
    output_input = args.output_dir.absolute()
    if not build_dir.is_dir():
        print(f"build directory does not exist: {build_dir}", file=sys.stderr)
        return 2
    if output_input.exists() or output_input.is_symlink():
        print(f"output directory already exists: {output_input}", file=sys.stderr)
        return 2
    try:
        raw_manifest = manifest_path.read_bytes()
        selections = validate_manifest(json.loads(raw_manifest))
        build_scope = unit_build_scope(build_dir)
    except (OSError, UnicodeDecodeError, json.JSONDecodeError, RunnerError) as error:
        print(f"invalid manifest or build scope: {error}", file=sys.stderr)
        return 2
    output_dir = output_input.resolve()
    if output_dir.exists():
        print(f"output directory already exists: {output_dir}", file=sys.stderr)
        return 2
    output_dir.parent.mkdir(parents=True, exist_ok=True)
    output_dir.mkdir()
    started_utc = datetime.now(timezone.utc).isoformat()

    uses_ctest = any(selection["suite"] in {"unit", "qml", "integration"} for selection in selections)
    ctest_entries = None
    discovery: dict[str, Any] = {}
    if uses_ctest:
        ctest_entries, discovery["ctest"] = _run_ctest_discovery(build_dir, output_dir / "_discovery_ctest", args.timeout)
    unit_classes = None
    if any(selection["suite"] == "unit" for selection in selections) and ctest_entries is not None:
        class_entry = ctest_entries.get("bitcoinqml_unit_tests")
        unit_classes, discovery["unit_classes"] = _run_unit_class_discovery(
            class_entry, build_dir, output_dir / "_discovery_unit_classes", args.timeout)
        if build_scope["mode"] == "focused" and unit_classes is not None and unit_classes != set(build_scope["compiled_classes"]):
            discovery["unit_classes"].update(status="fail", issue="binary classes differ from the configured focused build scope")
            unit_classes = None
    elif any(selection["suite"] == "unit" for selection in selections):
        discovery["unit_classes"] = {"status": "fail", "issue": "CTest discovery did not produce a unit command"}

    results: list[dict[str, Any]] = []
    for selection in selections:
        try:
            result = _run_selection(selection, build_dir, output_dir, ctest_entries, unit_classes, args.timeout)
        except (OSError, RunnerError) as error:
            result = {"id": selection["id"], "suite": selection["suite"], "expected": selection["expected"],
                      "status": "fail", "issues": [str(error)], "process": None, "bodies": None}
            entry_dir = output_dir / selection["id"]
            if not entry_dir.exists():
                entry_dir.mkdir()
        (output_dir / selection["id"] / "result.json").write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
        results.append(result)
        print(f"{result['status'].upper()} {selection['id']}: " + ("; ".join(result["issues"]) if result["issues"] else f"{result['bodies']['body_count']} bodies"))

    overall = "pass" if all(result["status"] == "pass" for result in results) else (
        "incomplete" if all(result["status"] in {"pass", "incomplete"} for result in results) else "fail")
    report = {
        "version": VERSION,
        "started_utc": started_utc,
        "finished_utc": datetime.now(timezone.utc).isoformat(),
        "build_dir": str(build_dir),
        "unit_build_scope": build_scope,
        "manifest": {"path": str(manifest_path), "sha256": _sha256(raw_manifest)},
        "output_dir": str(output_dir),
        "timeout_seconds": args.timeout,
        "discovery": discovery,
        "summary": {"status": overall, "selections": len(results),
                    "passed": sum(result["status"] == "pass" for result in results),
                    "failed": sum(result["status"] == "fail" for result in results),
                    "incomplete": sum(result["status"] == "incomplete" for result in results)},
        "results": results,
    }
    (output_dir / "report.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(f"Focused run {overall}: {report['summary']['passed']} passed, {report['summary']['failed']} failed, {report['summary']['incomplete']} incomplete")
    return 0 if overall == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
