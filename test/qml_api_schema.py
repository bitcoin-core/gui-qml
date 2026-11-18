#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Check real moc projection schemas and selected pure-domain include closures.

Source declarations are matched to generated Qt metadata. This structural gate
does not prove notifier emission, thread affinity or backend atomicity; real
consumer/manual/physical-thread tests remain necessary. No fake QObject is built.
"""

import argparse
import copy
import hashlib
import json
from pathlib import Path
import re


class ContractError(RuntimeError):
    pass


def require(condition, message):
    if not condition:
        raise ContractError(message)


def tokens(source):
    """Declaration lexer: comments are discarded, quoted contents stay opaque."""
    source = source.replace("\\\n", "")
    result, index = [], 0
    while index < len(source):
        if source[index].isspace():
            index += 1
        elif source.startswith("//", index):
            end = source.find("\n", index)
            index = len(source) if end < 0 else end
        elif source.startswith("/*", index):
            end = source.find("*/", index + 2)
            require(end >= 0, "Unclosed C++ comment")
            index = end + 2
        elif source[index] in "\"'":
            start, quote = index, source[index]
            index += 1
            while index < len(source) and source[index] != quote:
                index += 2 if source[index] == "\\" else 1
            require(index < len(source), "Unclosed C++ literal")
            index += 1
            result.append(source[start:index])
        else:
            require(not source.startswith('R"', index), "Raw literals need explicit lexer support")
            match = re.match(r"[A-Za-z_]\w*|\d+|::", source[index:])
            value = match.group() if match else source[index]
            result.append(value)
            index += len(value)
    return result


def closing(stream, start, left, right):
    require(stream[start] == left, "Missing opening delimiter")
    depth = 0
    for index in range(start, len(stream)):
        depth += stream[index] == left
        depth -= stream[index] == right
        if depth == 0:
            return index
    raise ContractError("Unclosed declaration delimiter")


def source_properties(source, name):
    stream, body = tokens(source), None
    for index in range(len(stream) - 1):
        if stream[index:index + 2] != ["class", name]:
            continue
        end = index + 2
        while end < len(stream) and stream[end] not in {"{", ";"}:
            end += 1
        if end < len(stream) and stream[end] == "{":
            require(body is None, f"Duplicate class definition: {name}")
            body = stream[end + 1:closing(stream, end, "{", "}")]
    require(body is not None, f"Missing class definition: {name}")
    properties = {}
    keywords = {"READ", "WRITE", "RESET", "NOTIFY", "REVISION", "DESIGNABLE", "SCRIPTABLE",
                "STORED", "USER", "CONSTANT", "FINAL", "REQUIRED", "BINDABLE", "MEMBER"}
    for index, value in enumerate(body):
        if value != "Q_PROPERTY":
            continue
        end = closing(body, index + 1, "(", ")")
        declaration = body[index + 2:end]
        split = next((i for i, item in enumerate(declaration) if item in keywords), None)
        require(split is not None and split >= 2, f"Unsupported Q_PROPERTY in {name}")
        prop = {"name": declaration[split - 1], "type": "".join(declaration[:split - 1]),
                "constant": "CONSTANT" in declaration[split:]}
        for key in ["READ", "WRITE", "MEMBER", "NOTIFY"]:
            if key in declaration[split:]:
                pos = declaration.index(key, split)
                require(pos + 1 < len(declaration), f"Missing {key} value")
                prop[key.lower()] = declaration[pos + 1]
        require(prop["name"] not in properties, f"Duplicate property: {name}.{prop['name']}")
        properties[prop["name"]] = prop
    require(properties, f"No properties selected for {name}")
    return properties


def check_class(name, actual, declared, policy):
    require(policy.get("writer_guard", "all") in {"all", "required"}, f"Unknown writer policy: {name}")
    require(actual.get("object") is True, f"{name} is not a real QObject schema")
    props = {p["name"]: p for p in actual.get("properties", [])}
    require(len(props) == len(actual.get("properties", [])), f"Duplicate moc property in {name}")
    require(set(props) == set(declared), f"Stale or incomplete moc properties for {name}")
    signals = {signal["name"] for signal in actual.get("signals", [])}
    for key, prop in props.items():
        for attribute in ["type", "constant", "read", "write", "member", "notify"]:
            value = prop.get(attribute, False if attribute == "constant" else None)
            expected = declared[key].get(attribute)
            if attribute == "type":
                # moc canonicalizes these built-in C++ spellings, rather than
                # changing the public value type (e.g. unsigned int -> uint).
                aliases = {"unsignedint": "uint", "unsignedshort": "ushort", "unsignedchar": "uchar",
                           "longlong": "qlonglong", "unsignedlonglong": "qulonglong"}
                value = re.sub(r"\s+", "", value)
                value, expected = aliases.get(value, value), aliases.get(expected, expected)
            require(value == expected, f"Source/moc mismatch: {name}.{key}.{attribute}")
        require(prop.get("read") and not prop.get("member"), f"Raw member exposed: {name}.{key}")
        if prop.get("write") and (policy.get("writer_guard", "all") == "all" or key in policy["required"]):
            require(prop["write"] == policy.get("writable", {}).get(key), f"Unapproved writer: {name}.{key}")
        require(prop.get("constant") or prop.get("notify") in signals, f"Missing real notifier: {name}.{key}")
        require(not (prop.get("constant") and prop.get("write")), f"Writable constant: {name}.{key}")
    for key, expected in policy["required"].items():
        require(key in props, f"Required API missing: {name}.{key}")
        require(props[key]["type"] == expected["type"] and props[key].get("constant", False) == expected["constant"],
                f"Authority/type changed: {name}.{key}")
    for method in actual.get("methods", []) + actual.get("slots", []):
        types = [method.get("returnType", ""), *(arg["type"] for arg in method.get("arguments", []))]
        require(not any(word in typ for typ in types for word in ["std::function", "execution::", "QThread", "QJSValue"])
                and all("interfaces::" not in typ or typ in policy.get("interface_values", []) for typ in types),
                f"Backend/execution transport exposed: {name}.{method['name']}")


def includes(source):
    stream, result = tokens(source), []
    for index in range(len(stream) - 2):
        if stream[index:index + 2] != ["#", "include"]:
            continue
        start = index + 2
        if stream[start].startswith('"'):
            result.append(stream[start][1:-1])
        else:
            require(stream[start] == "<", "Macro include needs explicit domain policy")
            end = stream.index(">", start + 1)
            result.append("".join(stream[start + 1:end]))
    return result


def domain_closure(root, policy, overrides=None):
    overrides = overrides or {}
    pending, visited = list(policy["domain_headers"]), {}
    while pending:
        name = pending.pop()
        if name in visited:
            continue
        path = root / name
        require(path.is_file() or name in overrides, f"Missing domain dependency: {name}")
        source = overrides.get(name, path.read_text(encoding="utf-8") if path.is_file() else "")
        deps = includes(source)
        visited[name] = deps
        for dependency in deps:
            if dependency.startswith("qml/"):
                require(dependency in policy["domain_headers"], f"Domain crosses owner/projection boundary: {name} -> {dependency}")
                pending.append(dependency)
            else:
                require(dependency in policy["value_includes"], f"Domain crosses platform/backend boundary: {name} -> {dependency}")
    return visited


def active_schema_path(build_dir, header, autogen, compilation):
    rows = [row for row in autogen["HEADERS"] if row[0].endswith("/" + header)]
    require(len(rows) == 1, f"Missing or duplicate current autogen input: {header}")
    moc = rows[0][2]
    require(f'#include "{moc}"' in compilation, f"Moc is not in current compilation: {header}")
    path = build_dir / "bitcoinqml_autogen" / (moc + ".json")
    require(path.is_file(), f"Current moc JSON missing: {header}")
    return path


def check_schema(root, build_dir, output):
    policy = json.loads((root / "test/qml_api_policy.json").read_text(encoding="utf-8"))
    require(policy.get("version") == 1 and policy.get("classes"), "Empty or unsupported API policy")
    configured = re.findall(r"^ENABLE_WALLET:BOOL=(ON|OFF)$", (build_dir / "CMakeCache.txt").read_text(encoding="utf-8"), re.M)
    require(len(configured) == 1, "Missing or ambiguous configured wallet variant")
    wallet_enabled = configured[0] == "ON"
    applicable = {name: spec for name, spec in policy["classes"].items()
                  if wallet_enabled or not spec["requires_wallet"]}
    autogen_path = build_dir / "CMakeFiles/bitcoinqml_autogen.dir/AutogenInfo.json"
    autogen = json.loads(autogen_path.read_text(encoding="utf-8"))
    compilation_path = build_dir / "bitcoinqml_autogen/mocs_compilation.cpp"
    compilation = compilation_path.read_text(encoding="utf-8")
    found = {}
    for header in sorted({spec["header"] for spec in applicable.values()}):
        path = active_schema_path(build_dir, header, autogen, compilation)
        metadata = json.loads(path.read_text(encoding="utf-8"))
        for cls in metadata.get("classes", []):
            name = cls.get("className")
            if name in applicable:
                require(name not in found, f"Duplicate actual moc class: {name}")
                require(metadata["inputFile"] == Path(policy["classes"][name]["header"]).name, f"Wrong moc input: {name}")
                found[name] = (cls, path)
    require(set(found) == set(applicable), f"Missing actual moc schemas: {sorted(set(applicable) - set(found))}")
    snapshots = {}
    for name, spec in applicable.items():
        declared = source_properties((root / spec["header"]).read_text(encoding="utf-8"), name)
        check_class(name, found[name][0], declared, spec)
        snapshots[name] = (found[name][0], declared, spec)
    closure, controls = domain_closure(root, policy), []

    def rejects(label, action):
        try:
            action()
        except ContractError:
            controls.append(label)
        else:
            raise ContractError(f"Negative control escaped: {label}")

    def mutation(name, field, change, source_change=False):
        actual, declared, spec = copy.deepcopy(snapshots[name])
        prop = next(p for p in actual["properties"] if p["name"] == field)
        change(prop)
        if source_change:
            declared[field] = {k: v for k, v in prop.items() if k in {"name", "type", "constant", "read", "write", "member", "notify"}}
        check_class(name, actual, declared, spec)

    rejects("stale-moc-notifier", lambda: mutation("SettingsEditorModel", "busy", lambda p: p.pop("notify")))
    rejects("source-and-moc-missing-notifier", lambda: mutation("SettingsEditorModel", "busy", lambda p: p.pop("notify"), True))
    if wallet_enabled:
        rejects("writable-operation-phase", lambda: mutation("WalletOperationModel", "phase", lambda p: p.update(write="setPhase"), True))
        rejects("constant-retargetable-result", lambda: mutation("WalletOperationModel", "resultWallet", lambda p: p.update(constant=True), True))
        rejects("untyped-live-selection", lambda: mutation("WalletSelectionModel", "selectedWallet", lambda p: p.update(type="QObject*"), True))
    rejects("writable-node-lifecycle", lambda: mutation("NodeModel", "lifecyclePhase", lambda p: p.update(write="setLifecyclePhase"), True))
    rejects("missing-signal", lambda: mutation("PeerSelectionModel", "connected", lambda p: p.update(notify="missingSignal"), True))
    rejects("writable-field-literal", lambda: mutation("SettingsEntryModel", "text", lambda p: p.update(write="setText"), True))
    actual, declared, spec = copy.deepcopy(snapshots["SettingsEditorModel"])
    actual.setdefault("methods", []).append({"name": "backend", "returnType": "interfaces::Node*"})
    rejects("public-Core-handle", lambda: check_class("SettingsEditorModel", actual, declared, spec))
    stale = copy.deepcopy(autogen)
    header = applicable["SettingsEditorModel"]["header"]
    stale["HEADERS"] = [row for row in stale["HEADERS"] if not row[0].endswith("/" + header)]
    rejects("unchanged-but-excluded-stale-moc", lambda: active_schema_path(build_dir, header, stale, compilation))
    for label, dependency in [("direct-QML-dependency", "qml/models/walletreceivemodel.h"),
                              ("direct-Core-dependency", "interfaces/wallet.h"), ("direct-I-O-dependency", "QFile")]:
        source = (root / "qml/persistenteditor.h").read_text(encoding="utf-8") + f"\n#include <{dependency}>\n"
        rejects(label, lambda text=source: domain_closure(root, policy, {"qml/persistenteditor.h": text}))
    source = (root / "qml/walletopeningstate.h").read_text(encoding="utf-8") + "\n#include <QThread>\n"
    rejects("transitive-thread-dependency", lambda: domain_closure(root, policy, {"qml/walletopeningstate.h": source}))
    require(source_properties('/* class Fake { Q_PROPERTY(bool busy READ busy) }; */\n'
                              'class Fake { Q_PROPERTY(bool busy READ busy NOTIFY changed) };', "Fake")["busy"]["notify"] == "changed",
            "Comment handling failed")
    report = {"status": "pass", "classes": len(found), "properties": sum(len(c[0]["properties"]) for c in snapshots.values()),
              "configured_wallet": wallet_enabled,
              "variant_inapplicable_classes": sorted(set(policy["classes"]) - set(applicable)),
              "current_autogen_info_sha256": hashlib.sha256(autogen_path.read_bytes()).hexdigest(),
              "current_mocs_compilation_sha256": hashlib.sha256(compilation_path.read_bytes()).hexdigest(),
              "class_names": sorted(found), "negative_controls": controls, "domain_include_closure": closure,
              "moc_inputs": [{"class": name, "file": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
                             for name, (_, path) in sorted(found.items())],
              "limits": "Structural/source-moc/API/include checks; not proof of notifier emission, affinity, backend atomicity or every caller."}
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"QML API schema gate: {report['classes']} real classes, {report['properties']} properties, {len(controls)} negative controls passed")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    check_schema(args.source_root, args.build_dir, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
