#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Build an explicitly scoped, test-only QML resource instrumentation overlay.

Requires the pinned Qt 6.10.2 qmldom and rcc, not a production Qt patch. qmldom's
AST dump is C-escaped XML-like text, NOT XML; the strict reader below deliberately
does not feed it to an XML parser. AST coordinates are UTF-16 code-unit offsets.

Contract: executable statement/binding/function-entry AST sites, conditional
arms, and explicit boolean/short-circuit/nullish decisions. A site's line is its
original starting line. This is NOT coverage of every textual line spanned by a
multiline expression, implicit exception edges, Qt binding machinery, native
components, generated code or files outside the explicit scope. No percentage
from this tool establishes the program's complete reachable-line obligation.
Direct function-valued QML bindings retain their syntactic FunctionExpression:
their declaration-line point measures invocation, not function allocation or
binding initialization. Ordinary function-entry points are measured separately.

Unknown AST kinds, unsupported decision forms, duplicate mappings, stale source
hashes, invalid coordinates and empty selections fail closed. Tool qualification
is independent of the measured results, and remains false in generated reports.
"""

import argparse
from collections import Counter, defaultdict
from dataclasses import dataclass, field
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

CONTRACT = "ast-entry-sites-and-explicit-boolean-decisions-v1"
HOOK = "__r10cov"
TOKEN = re.compile(r"off:(\d+) len:(\d+) l:(\d+) c:(\d+) \Z")
BINARY_OPERATORS = dict(enumerate([
    "+", "&&", "&=", "=", "&", "|", "^", "-=", "/", "/=", "==", "**", "**=",
    ">=", ">", "in", "+=", "instanceof", "<=", "<<", "<<=", "<", "%", "%=", "*",
    "*=", "!=", "||", "|=", ">>", ">>=", "===", "!==", "-", ">>>", ">>>=", "^=", "as", "??",
]))
# Finite, audited grammar. Unknown forms must be reviewed before extending it.
KNOWN = set("""
Node ArgumentList ArrayMemberExpression ArrayPattern BinaryExpression Block
CallExpression ConditionalExpression ExpressionStatement FalseLiteral
FieldMemberExpression Finally FormalParameterList FunctionDeclaration
FunctionExpression IdentifierExpression IdentifierPropertyName IfStatement
NestedExpression NotExpression NullExpression NumericLiteral ObjectPattern
PatternElement PatternElementList PatternProperty PatternPropertyList
PostIncrementExpression PostDecrementExpression PreIncrementExpression
PreDecrementExpression ReturnStatement StatementList StringLiteral
StringLiteralPropertyName TrueLiteral TryStatement Type UiHeaderItemList
UiImport UiObjectBinding UiObjectDefinition UiObjectInitializer UiObjectMemberList
UiParameterList UiProgram UiPublicMember UiQualifiedId UiScriptBinding
UiSourceElement UiVersionSpecifier UnaryMinusExpression UnaryPlusExpression
VariableDeclarationList VariableStatement ForStatement WhileStatement
DoWhileStatement BreakStatement ContinueStatement EmptyStatement ThrowStatement
TypeOfExpression VoidExpression TildeExpression NewExpression
NewMemberExpression RegExpLiteral ThisExpression UiArrayBinding UiArrayMemberList
UiEnumDeclaration UiEnumMemberList UiPragma UiAnnotation UiRequired UiInlineComponent
SwitchStatement CaseBlock CaseClause CaseClauses DefaultClause ForEachStatement
TemplateLiteral Catch TypeAnnotation DeleteExpression
""".split())
UNSUPPORTED = {
    "WithStatement", "LabelledStatement", "YieldExpression",
    "AwaitExpression", "TaggedTemplate",
    "ClassDeclaration", "ClassExpression", "SuperLiteral", "ImportDeclaration",
    "ExportDeclaration", "OptionalExpression", "OptionalChain",
}


class MappingError(RuntimeError):
    pass


def require(condition, message):
    if not condition:
        raise MappingError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


@dataclass
class Node:
    kind: str
    attrs: dict
    children: list = field(default_factory=list)
    parent: object = None
    span: tuple | None = None


def parse_dump(text):
    stack, roots, cursor = [], [], 0
    attribute = re.compile(r'\s+(\w+)=(?:"((?:\\.|[^"\\])*)"|([^\s>]+))', re.S)
    while cursor < len(text):
        start = text.find("<", cursor)
        if start < 0:
            require(not text[cursor:].strip(), "Unparsed AST suffix")
            break
        require(not text[cursor:start].strip(), "Unparsed text between AST nodes")
        end, quoted, escaped = start + 1, False, False
        while end < len(text):
            char = text[end]
            if escaped:
                escaped = False
            elif char == "\\" and quoted:
                escaped = True
            elif char == '"':
                quoted = not quoted
            elif char == ">" and not quoted:
                break
            end += 1
        require(end < len(text) and not quoted, "Truncated AST tag")
        body = text[start + 1:end]
        if body.startswith("/"):
            require(stack and stack[-1].kind == body[1:].strip(), "Mismatched AST close tag")
            stack.pop()
        else:
            match = re.fullmatch(r"(\w+)(.*)", body, re.S)
            require(match is not None, "Invalid AST tag")
            kind, rest = match.groups()
            attrs, at = {}, 0
            while rest[at:].strip():
                value = attribute.match(rest, at)
                require(value is not None, f"Unparsed AST attribute in {kind}")
                require(value[1] not in attrs, f"Duplicate AST attribute in {kind}")
                attrs[value[1]] = value[2] if value[2] is not None else value[3]
                at = value.end()
            node = Node(kind, attrs, parent=stack[-1] if stack else None)
            (stack[-1].children if stack else roots).append(node)
            stack.append(node)
        cursor = end + 1
    require(not stack and len(roots) == 1, "Missing or multiple AST roots")
    return roots[0]


def walk(node):
    yield node
    for child in node.children:
        yield from walk(child)


def children(node):
    """The dumper wraps linked AST lists in Node wrappers containing many nodes."""
    result = []
    for child in node.children:
        if child.kind == "Node":
            items = children(child)
            # Linked template segments are one expression, not many operands.
            result.extend([child] if len(items) > 1 and all(item.kind == "TemplateLiteral" for item in items) else items)
        else:
            result.append(child)
    return result


def parent(node):
    result = node.parent
    while result is not None and result.kind == "Node":
        result = result.parent
    return result


def token(node, key):
    value = node.attrs.get(key, "")
    if not value:
        return None
    match = TOKEN.fullmatch(value)
    require(match is not None, f"Invalid token location: {node.kind}.{key}")
    return tuple(map(int, match.groups()))


def locate(tree, text):
    encoded = text.encode("utf-16-le")
    # Coordinate validation includes astral characters, lines, columns, and
    # surrogate boundaries. A stale or differently normalized input fails.
    line_starts = [0]
    for index in range(0, len(encoded), 2):
        if encoded[index:index + 2] == b"\n\x00":
            line_starts.append(index // 2 + 1)
    for node in reversed(list(walk(tree))):
        spans = []
        for key in node.attrs:
            if not key.endswith("Token"):
                continue
            if node.kind == "UiObjectBinding" and key == "hasOnToken":
                require(node.attrs[key] in {"true", "false"}, "Unknown hasOnToken flag")
                continue
            location = token(node, key)
            if location is None:
                continue
            offset, length, line, column = location
            require(0 <= offset <= offset + length <= len(encoded) // 2,
                    f"Out-of-range token: {node.kind}.{key}")
            try:
                encoded[2 * offset:2 * (offset + length)].decode("utf-16-le")
            except UnicodeDecodeError as error:
                raise MappingError("Token splits a UTF-16 surrogate pair") from error
            require(1 <= line <= len(line_starts) and line_starts[line - 1] + column - 1 == offset,
                    f"Token line/column mismatch: {node.kind}.{key}")
            spans.append((offset, offset + length))
        spans.extend(child.span for child in node.children if child.span)
        if spans:
            node.span = min(item[0] for item in spans), max(item[1] for item in spans)
    return encoded


class Instrumenter:
    def __init__(self, path, original, tree, first_id, metadata_only=False):
        self.path = path
        self.original = original
        self.tree = tree
        self.data = locate(tree, original)
        self.first_id = first_id
        self.points = []
        self.metadata = []
        self.native_initializers = []
        self.completion_blocks = []
        self.function_bindings = []
        self.function_binding_probes = defaultdict(list)
        self.body_probes = defaultdict(list)
        self.wrappers = []
        self.insertions = defaultdict(list)
        self.histogram = Counter(node.kind for node in walk(tree) if node.kind != "Node")
        self.metadata_only = metadata_only

    def slice(self, span):
        return self.data[2 * span[0]:2 * span[1]].decode("utf-16-le")

    def point(self, kind, span, role):
        require(span is not None and span[0] <= span[1], f"Unmapped {role}")
        prefix = self.slice((0, span[0]))
        line = prefix.count("\n") + 1
        column = len(prefix.rsplit("\n", 1)[-1].encode("utf-16-le")) // 2 + 1
        identifier = self.first_id + len(self.points)
        coordinate = f"{self.path}:{span[0]}:{span[1]}:{kind}:{role}"
        require(not any(point["coordinate_id"] == coordinate for point in self.points), "Duplicate coordinate ID")
        self.points.append({"id": identifier, "coordinate_id": coordinate, "file": self.path,
                            "line": line, "column": column, "start_utf16": span[0],
                            "end_utf16": span[1], "kind": kind, "role": role})
        return identifier

    def wrap(self, span, prefix, suffix):
        require(span and span[0] < span[1], "Cannot wrap an empty source span")
        self.wrappers.append((span[0], span[1], prefix, suffix, len(self.wrappers)))

    @staticmethod
    def probe_statement(expression):
        # An expression statement evaluates to undefined for a void native
        # probe. That value can replace the previous completion of a QML script
        # binding, including an empty nested block or the end of an iteration.
        # A VariableStatement has Empty completion, preserving StatementList /
        # loop / try-finally completion propagation. The function-local var is
        # reserved, including decoded/escaped identifier spellings checked below.
        return f"var {HOOK}_unused={expression};"

    @staticmethod
    def empty_completion_block(block):
        # Match the pinned qv4codegen.cpp completionState classification.
        # Unlike completionStatement, its nested-block scan does not skip
        # VariableStatement. Thus a new var inside an empty block changes Qt's
        # choice of the original value-carrying statement, despite Empty
        # completion in the language. Preserve the original empty block shape.
        for part in children(block):
            if part.kind != "StatementList":
                continue
            for statement in children(part):
                if statement.kind in {"EmptyStatement", "FunctionDeclaration"}:
                    continue
                if statement.kind == "Block" and Instrumenter.empty_completion_block(statement):
                    continue
                return False
        return True

    def block_probe(self, block, expression):
        target = block
        if self.empty_completion_block(block):
            # Only empty blocks/statements and function declarations occur in
            # this chain, so every child block is entered with its outer block.
            # Emit probes outside the chain instead of adding its first var.
            while parent(target) and parent(target).kind == "StatementList":
                outer = parent(parent(target))
                if outer and outer.kind == "Block" and self.empty_completion_block(outer):
                    target = outer
                else:
                    break
            owner = parent(target)
            if owner and owner.kind == "StatementList":
                self.insertions[target.span[0]].append(self.probe_statement(expression))
                return
            if owner and owner.kind in {"IfStatement", "WhileStatement", "DoWhileStatement", "ForStatement", "ForEachStatement"}:
                # A matched always-true if introduces no new StatementList or
                # scope; its empty else also preserves an enclosing else owner.
                self.wrap(target.span, f"if(({expression},true))", "else{}")
                return
        opening = token(target, "lbraceToken")
        self.insertions[opening[0] + opening[1]].append(self.probe_statement(expression))

    def check_binding_completions(self):
        """Reject pinned-VM contexts without a completion-preserving mapping.

        Function bodies do not expose StatementList completion. QML script
        blocks can, including blocks whose property name resembles a handler;
        check every such block instead of inferring types from its name.
        """
        controls = {"ForEachStatement", "ForStatement", "WhileStatement", "DoWhileStatement", "SwitchStatement"}

        def scan(node, active):
            if node.kind in {"FunctionDeclaration", "FunctionExpression"}:
                return
            parts = children(node)
            if node.kind == "ForEachStatement":
                require(not active, "Pinned Qt binding completion: nested for-in/of needs a separate mapping")
                require(parts[-1].kind in {"Block", "IfStatement", "ExpressionStatement", "ReturnStatement",
                                          "ThrowStatement", "BreakStatement", "ContinueStatement", "VariableStatement", "EmptyStatement"},
                        "Pinned Qt binding completion: complex for-in/of body needs a separate mapping")
            if node.kind == "TryStatement" and active:
                bodies = [part for part in parts if part.kind == "Block"]
                for catch in (part for part in parts if part.kind == "Catch"):
                    bodies.extend(part for part in children(catch) if part.kind == "Block")
                require(all(any(part.kind == "StatementList" for part in children(body)) for body in bodies),
                        "Pinned Qt binding completion: empty try/catch body in a loop or switch needs a separate mapping")
            next_active = active + (node.kind,) if node.kind in controls else active
            for child in parts:
                scan(child, next_active)

        for node in walk(self.tree):
            owner = parent(node)
            if node.kind == "Block" and owner and owner.kind in {"UiScriptBinding", "UiPublicMember"}:
                scan(node, ())
                self.completion_blocks.append({"start_utf16": node.span[0], "end_utf16": node.span[1],
                                               "policy": "pinned-Qt-completion-v1", "checked": True})

    def single_body_probes(self, node, probes):
        if node.kind == "ExpressionStatement":
            expression = children(node)
            require(len(expression) == 1, "Unmapped single expression statement")
            self.wrap(expression[0].span, "(" + ",".join(probes) + ",(", "))")
        elif node.kind == "VariableStatement":
            kind = token(node, "declarationKindToken")
            require(kind is not None, "Unmapped single variable statement")
            name = f"{HOOK}_unused_{len(self.points)}"
            self.insertions[kind[0] + kind[1]].append(f" {name}=(" + ",".join(probes) + "),")
        else:
            require(node.kind in {"BreakStatement", "ContinueStatement", "ReturnStatement", "ThrowStatement", "EmptyStatement"},
                    f"Unmapped single statement probe: {node.kind}")
            # A matched if adds no StatementList, lexical scope or completion
            # register reset. Always true, including when a probe returns void.
            end = self.slice((node.span[1] - 1, node.span[1]))
            self.wrap(node.span, "if((" + ",".join(probes) + ",true))", ("" if end == ";" else ";") + "else{}")

    def expression_site(self, node, role):
        identifier = self.point("line", node.span, role)
        self.wrap(node.span, f"({HOOK}.line({identifier}),(", "))")

    def metadata_reference(self, node, kind):
        allowed = {"Node", "IdentifierExpression", "FieldMemberExpression"}
        require(all(part.kind in allowed for part in walk(node)), "Non-reference syntax in QML metadata")
        self.metadata.append({"kind": kind, "start_utf16": node.span[0],
                              "end_utf16": node.span[1], "source": self.slice(node.span)})

    def decision(self, node, kind, role):
        identifier = self.point(kind, node.span, role)
        self.wrap(node.span, f"{HOOK}.{kind}({identifier},(", "))")

    def parameter_decisions(self, function, offset):
        """Map the supported trailing scalar default without rewriting bindings.

        Non-simple JS parameter lists have an unmapped arguments object. The
        initializer records the default arm before evaluation (also on throw);
        function entry records the supplied arm. Only one trailing default is
        supported, so no later initializer can throw before that supplied-arm
        probe. Arrows, rest/destructuring and arguments shadowing fail closed.
        """
        def belongs_to_function(node):
            owner = parent(node)
            while owner and owner.kind not in {"FunctionDeclaration", "FunctionExpression"}:
                owner = parent(owner)
            return owner is function

        # qmldom represents each link as a distinct FormalParameterList, not
        # one node containing every argument. Do not reset the index per link.
        elements = [element for node in walk(function)
                    if node.kind == "FormalParameterList" and belongs_to_function(node)
                    for element in children(node)]
        elements.sort(key=lambda element: element.span[0])
        require(all(element.kind == "PatternElement" and element.attrs.get("bindingIdentifier")
                    for element in elements), "Destructuring/rest parameters need independent mapping")
        defaults = [(index, element, [part for part in children(element) if part.kind != "TypeAnnotation"])
                    for index, element in enumerate(elements)]
        defaults = [entry for entry in defaults if entry[2]]
        if not defaults:
            return
        require(len(defaults) == 1 and defaults[0][0] == len(elements) - 1,
                "Only one trailing default parameter has complete decision mapping")
        require(function.attrs.get("isArrowFunction") == "false",
                "Arrow default parameters need independent original-argument mapping")
        require(not any(node.attrs.get("bindingIdentifier") == "arguments"
                        or node.kind in {"FunctionDeclaration", "FunctionExpression"} and node.attrs.get("name") == "arguments"
                        for node in walk(function)), "Shadowed arguments prevents original-parameter mapping")
        index, element, expressions = defaults[0]
        require(len(expressions) == 1, "Unmapped default parameter initializer")
        initializer = expressions[0]
        # These cover the current literal/reference initializers. Arbitrary
        # parameter expressions could mutate arguments or introduce additional
        # default/destructuring decisions; do not claim those are mapped.
        require(all(node.kind in {"Node", "IdentifierExpression", "FieldMemberExpression", "NumericLiteral",
                                  "StringLiteral", "TrueLiteral", "FalseLiteral", "NullExpression",
                                  "UnaryMinusExpression", "UnaryPlusExpression"} for node in walk(initializer)),
                "Complex default initializers need independent argument/abrupt-completion mapping")
        require(not any(node.attrs.get("name") in {"arguments", "caller", "callee"} for node in walk(initializer)),
                "Reflective parameter initializers prevent original-argument mapping")
        identifier = self.point("truth", initializer.span, "trailing-default-parameter")
        self.wrap(initializer.span, f"({HOOK}.truth({identifier},true),(", "))")
        self.insertions[offset].append(f";if(arguments[{index}]!==void 0){{" +
                                       self.probe_statement(f"{HOOK}.truth({identifier},false)") + "}")

    def native_list_roles(self):
        """Preserve native ListElement grammar and retain its measurement gap.

        Qt's ListModel custom parser accepts literal/translation AST roots, not
        arbitrary JavaScript comma wrappers. These are native initialization
        spans, not proved executed sites or a complete-line coverage exemption.
        """
        opaque = set()
        for binding in walk(self.tree):
            if binding.kind != "UiScriptBinding":
                continue
            owner = parent(binding)
            while owner and owner.kind not in {"UiObjectDefinition", "UiObjectBinding"}:
                owner = parent(owner)
            if owner is None:
                continue
            names = [node for node in children(owner) if node.kind == "UiQualifiedId"]
            if not 1 <= len(names) <= 2 or self.slice(names[-1].span) != "ListElement":
                continue
            list_owner = parent(owner)
            while list_owner and list_owner.kind not in {"UiObjectDefinition", "UiObjectBinding"}:
                list_owner = parent(list_owner)
            list_names = [node for node in children(list_owner) if node.kind == "UiQualifiedId"] if list_owner else []
            require(1 <= len(list_names) <= 2 and self.slice(list_names[-1].span) == "ListModel",
                    "Native ListElement outside a reviewed ListModel context")
            parts = children(binding)
            require(len(parts) == 2 and parts[0].kind == "UiQualifiedId" and parts[1].kind == "ExpressionStatement",
                    "Unreviewed native ListElement role declaration")
            values = children(parts[1])
            require(len(values) == 1, "Unmapped native ListElement value")
            value = values[0]
            literal = value.kind in {"StringLiteral", "NumericLiteral", "TrueLiteral", "FalseLiteral", "NullExpression"}
            if value.kind in {"UnaryMinusExpression", "UnaryPlusExpression"}:
                operands = children(value)
                literal = len(operands) == 1 and operands[0].kind == "NumericLiteral"
            translated = False
            if value.kind == "CallExpression":
                call = children(value)
                if len(call) == 2 and call[0].kind == "IdentifierExpression" and call[0].attrs.get("name") == "qsTr" and call[1].kind == "ArgumentList":
                    arguments = children(call[1])
                    translated = len(arguments) == 1 and arguments[0].kind == "StringLiteral"
            require(literal or translated, "Unreviewed native ListElement initializer; cannot claim its JavaScript decisions")
            opaque.update(id(node) for node in walk(binding))
            self.native_initializers.append({"kind": "native-ListElement-role-initialization",
                                             "start_utf16": value.span[0], "end_utf16": value.span[1],
                                             "role": self.slice(parts[0].span), "source": self.slice(value.span),
                                             "measurement": "unmeasured native initialization; no executed-line or branch claim"})
        return opaque

    def directives(self):
        result = set()
        for node in walk(self.tree):
            if node.kind not in {"FunctionDeclaration", "FunctionExpression"}:
                continue
            for body in children(node):
                if body.kind != "StatementList":
                    continue
                for statement in children(body):
                    expr = children(statement)
                    if statement.kind == "ExpressionStatement" and len(expr) == 1 and expr[0].kind == "StringLiteral":
                        result.add(id(statement))
                    else:
                        break
        return result

    def run(self):
        require(HOOK not in self.original, f"Reserved coverage hook occurs in {self.path}")
        require(not any(HOOK in value for node in walk(self.tree) for value in node.attrs.values()),
                f"Reserved coverage prefix occurs in decoded AST of {self.path}")
        # The pinned dumper omits optionalToken/isOptional for member/call nodes.
        # Conservative rejection (including occurrences in text/comments) avoids
        # treating an unreported short-circuit edge as measured ordinary access.
        require("?." not in self.original, "Optional chaining is not completely represented by the pinned AST dump")
        unknown = sorted(set(self.histogram) - KNOWN)
        require(not unknown, f"Unsupported AST kinds in {self.path}: {', '.join(unknown)}")
        if self.metadata_only:
            allowed = {"Node", "UiProgram", "UiHeaderItemList", "UiImport", "UiObjectDefinition", "UiObjectInitializer",
                       "UiObjectMemberList", "UiPublicMember", "UiQualifiedId", "UiRequired"}
            require(all(node.kind in allowed for node in walk(self.tree)),
                    "Explicit metadata-only file contains executable or unreviewed AST syntax")
            require(all(node.attrs.get("type") == "Property" and node.attrs.get("isRequired") == "true"
                        and not node.attrs.get("colonToken")
                        and all(child.kind == "UiQualifiedId" and not children(child) for child in children(node))
                        for node in walk(self.tree) if node.kind == "UiPublicMember"),
                    "Explicit metadata-only property contains a value or unreviewed child")
            self.metadata.append({"kind": "explicit-reviewed-metadata-only-file", "start_utf16": 0,
                                  "end_utf16": len(self.data) // 2, "source_sha256": digest(self.original.encode("utf-8"))})
            return self.original
        self.check_binding_completions()
        native_roles = self.native_list_roles()
        directives = self.directives()
        contexts, concise_returns = {}, set()
        for node in walk(self.tree):
            if node.kind in {"SwitchStatement", "ForEachStatement"}:
                kind = "switch" if node.kind == "SwitchStatement" else "iteration"
                contexts[id(node)] = self.point(kind, node.span, node.kind)
            elif node.kind == "TryStatement" and any(child.kind == "Catch" for child in children(node)):
                contexts[id(node)] = self.point("caught", node.span, "try-catch")
        for node in walk(self.tree):
            if id(node) in native_roles:
                continue
            kind, parts, ancestor = node.kind, children(node), parent(node)
            if kind == "IdentifierExpression":
                require(node.attrs.get("name") not in {"eval", "Function"}, "Runtime-generated JavaScript needs an independent source map")
            if kind == "TypeAnnotation":
                require(all(part.kind in {"Node", "TypeAnnotation", "Type", "UiQualifiedId"} for part in walk(node)),
                        "Executable or unreviewed syntax in type annotation")
                self.metadata.append({"kind": "qml-type-annotation", "start_utf16": node.span[0],
                                      "end_utf16": node.span[1], "source": self.slice(node.span)})
            elif kind == "DeleteExpression":
                # A delete operand must remain a Reference. Wrapping a member
                # operand as a comma expression changes deletion semantics.
                # The enclosing statement/binding owns its entry point and
                # traversal still maps decisions inside the original operand.
                require(len(parts) == 1, "Unmapped delete operand")
            elif kind == "SwitchStatement":
                require(len(parts) == 2 and parts[1].kind == "CaseBlock", "Unmapped switch")
                identifier = contexts[id(node)]
                self.wrap(parts[0].span, f"{HOOK}.switchValue({identifier},(", "))")
                self.wrap(node.span, "{" + self.probe_statement(f"{HOOK}.enter({identifier})") + "try{",
                          "}finally{" + self.probe_statement(f"{HOOK}.leave({identifier})") + "}}")
            elif kind in {"CaseClause", "DefaultClause"}:
                owner = ancestor
                while owner and owner.kind != "SwitchStatement":
                    owner = parent(owner)
                require(owner is not None and id(owner) in contexts, "Unmapped switch arm owner")
                if kind == "CaseClause":
                    require(parts and parts[0].kind != "StatementList", "Unmapped switch comparison")
                    identifier = self.point("case", parts[0].span, "switch-case-strict-equality")
                    self.wrap(parts[0].span, f"{HOOK}.caseValue({identifier},{contexts[id(owner)]},(", "))")
                colon = token(node, "colonToken")
                require(colon is not None, "Unmapped switch arm colon")
                identifier = self.point("line", node.span, "switch-arm-entry")
                self.insertions[colon[0] + colon[1]].append(self.probe_statement(f"{HOOK}.line({identifier})"))
            elif kind == "ForEachStatement":
                require(len(parts) == 3 and parts[0].kind == "PatternElement" and not children(parts[0]),
                        "Destructuring/assignment iteration bindings need additional abrupt-completion mapping")
                where = token(node, "inOfToken")
                require(where and self.slice((where[0], where[0] + where[1])) in {"in", "of"}, "Unmapped iterator syntax")
                identifier = contexts[id(node)]
                self.wrap(node.span, "{" + self.probe_statement(f"{HOOK}.enter({identifier})") + "try{",
                          ";" + self.probe_statement(f"{HOOK}.exhausted({identifier})") + "}finally{" +
                          self.probe_statement(f"{HOOK}.leave({identifier})") + "}}")
                body = parts[-1]
                if body.kind == "Block":
                    self.block_probe(body, f"{HOOK}.step({identifier})")
                elif body.kind == "IfStatement":
                    self.wrap(children(body)[0].span, f"({HOOK}.step({identifier}),(", "))")
                elif body.kind == "EmptyStatement":
                    self.single_body_probes(body, [f"{HOOK}.step({identifier})"])
                elif body.kind in {"ExpressionStatement", "VariableStatement", "ReturnStatement", "ThrowStatement", "BreakStatement", "ContinueStatement"}:
                    self.body_probes[id(body)].append(f"{HOOK}.step({identifier})")
                else:
                    self.wrap(body.span, "{" + self.probe_statement(f"{HOOK}.step({identifier})"), "}")
            elif kind == "TryStatement" and id(node) in contexts:
                identifier = contexts[id(node)]
                self.wrap(node.span, "{" + self.probe_statement(f"{HOOK}.enter({identifier})") + "try{",
                          "}finally{" + self.probe_statement(f"{HOOK}.leave({identifier})") + "}}")
                catch = next(part for part in parts if part.kind == "Catch")
                blocks = [part for part in children(catch) if part.kind == "Block"]
                require(len(blocks) == 1 and all(part.kind in {"Block", "PatternElement"} for part in children(catch)),
                        "Unsupported catch binding")
                require(not any(part.kind == "PatternElement" and children(part) for part in children(catch)),
                        "Destructuring catch bindings unsupported")
                opening = token(blocks[0], "lbraceToken")
                self.insertions[opening[0] + opening[1]].append(self.probe_statement(f"{HOOK}.caught({identifier})"))
            elif kind == "BinaryExpression":
                operator = node.attrs.get("op")
                try:
                    expected = BINARY_OPERATORS[int(operator, 16)]
                except (ValueError, TypeError, KeyError) as error:
                    raise MappingError("Unknown pinned binary operator") from error
                where = token(node, "operatorToken")
                require(where and self.slice((where[0], where[0] + where[1])) == expected,
                        "Pinned operator enum/source mismatch")
                if operator in {"1", "1b", "26"}:  # &&, ||, ?? in pinned QSOperator::Op
                    require(len(parts) == 2, "Unmapped binary operands")
                    self.decision(parts[0], "nullish" if operator == "26" else "truth", expected)
                    self.expression_site(parts[1], f"{expected}-right")
            elif kind == "ConditionalExpression":
                require(len(parts) == 3, "Unmapped conditional expression")
                self.decision(parts[0], "truth", "conditional")
                self.expression_site(parts[1], "conditional-true")
                self.expression_site(parts[2], "conditional-false")
            elif kind in {"IfStatement", "WhileStatement", "DoWhileStatement"}:
                require(len(parts) >= 2, f"Unmapped {kind} condition")
                self.decision(parts[-1] if kind == "DoWhileStatement" else parts[0], "truth", kind)
            elif kind == "ForStatement":
                first, second = token(node, "firstSemicolonToken"), token(node, "secondSemicolonToken")
                require(first and second, "Unmapped for semicolons")
                conditions = [p for p in parts if p.span and first[0] < p.span[0] and p.span[1] <= second[0]]
                require(len(conditions) == 1, "Unconditional or unmapped for-loop condition requires an explicit policy")
                self.decision(conditions[0], "truth", "ForStatement")
            elif kind in {"FunctionDeclaration", "FunctionExpression"}:
                require(node.attrs.get("isGenerator") == "false", "Generator functions are unsupported")
                opening, closing = token(node, "lbraceToken"), token(node, "rbraceToken")
                require(opening and closing, "Unmapped function body")
                concise = opening[1] == closing[1] == 0 and node.attrs.get("isArrowFunction") == "true"
                require(concise or self.slice((opening[0], opening[0] + opening[1])) == "{", "Unmapped function opening brace")
                offset = opening[0] + opening[1]
                for body in parts:
                    if body.kind == "StatementList":
                        for statement in children(body):
                            if id(statement) in directives:
                                offset = statement.span[1]
                            else:
                                break
                self.parameter_decisions(node, offset)
                if concise:
                    bodies = [body for body in parts if body.kind == "StatementList"]
                    require(len(bodies) == 1, "Unmapped concise arrow body")
                    statements = children(bodies[0])
                    require(len(statements) == 1 and statements[0].kind == "ReturnStatement", "Unmapped synthetic arrow return")
                    returned = children(statements[0])
                    require(len(returned) == 1 and token(statements[0], "returnToken")[1] == 0,
                            "Unexpected concise arrow return token")
                    concise_returns.add(id(statements[0]))
                    for identifier in self.function_binding_probes[id(node)]:
                        self.wrap(returned[0].span, f"({HOOK}.line({identifier}),(", "))")
                    self.expression_site(returned[0], "function-entry")
                else:
                    identifier = self.point("line", (opening[0], closing[0] + closing[1]), "function-entry")
                    self.insertions[offset].extend(";" + self.probe_statement(f"{HOOK}.line({binding_id})")
                                                   for binding_id in self.function_binding_probes[id(node)])
                    self.insertions[offset].append(";" + self.probe_statement(f"{HOOK}.line({identifier})"))
            elif kind in {"ArrayPattern", "ObjectPattern"}:
                require(node.attrs.get("parseMode") == "0", "Destructuring patterns need explicit default-branch mapping")
            elif kind == "FieldMemberExpression":
                require(not node.attrs.get("isOptional", "false") == "true", "Optional chaining unsupported")
            elif kind == "Block":
                # Block entry preserves lexical scope and counts even empty handlers.
                opening = token(node, "lbraceToken")
                require(opening is not None, "Unmapped block entry")
                lists = [part for part in parts if part.kind == "StatementList"]
                if ancestor and ancestor.kind in {"UiScriptBinding", "UiPublicMember"} and lists:
                    statements = children(lists[0])
                    if statements and statements[0].kind == "ExpressionStatement":
                        expressions = children(statements[0])
                        require(not (expressions and expressions[0].kind == "StringLiteral"),
                                "A directive-leading QML script block needs separate completion/strictness qualification")
                identifier = self.point("line", node.span, "block-entry")
                self.block_probe(node, f"{HOOK}.line({identifier})")
            elif kind.endswith("Statement") and kind not in {"EmptyStatement"}:
                if id(node) in directives or id(node) in concise_returns:
                    continue  # Keep the JS directive prologue intact.
                if kind == "ExpressionStatement" and ancestor and ancestor.kind in {"UiScriptBinding", "UiPublicMember"}:
                    require(len(parts) == 1, "Unmapped binding expression")
                    type_token = token(ancestor, "typeToken")
                    if ancestor.kind == "UiPublicMember" and type_token and self.slice((type_token[0], type_token[0] + type_token[1])) == "alias":
                        # Alias references are declarative metadata. Turning
                        # them into comma expressions makes valid QML invalid.
                        self.metadata_reference(parts[0], "qml-alias-reference")
                        continue
                    if ancestor.kind == "UiScriptBinding":
                        names = [p for p in children(ancestor) if p.kind == "UiQualifiedId"]
                        if names and self.slice(names[0].span) == "id":
                            self.metadata_reference(parts[0], "qml-id-declaration")
                            continue  # QML object identifier declarations are metadata, not JS.
                    if parts[0].kind == "FunctionExpression":
                        # Qt's IRBuilder::setBindingValue sets IsFunctionExpression
                        # only for this exact AST root. A comma wrapper removes
                        # that flag and turns a signal handler into an expression
                        # returning an uncalled function. Preserve the root for
                        # every QML binding, without guessing property types from
                        # handler-like names. This original declaration-line site
                        # measures invocation; initialization remains unmeasured.
                        identifier = self.point("line", parts[0].span, "qml-function-binding-invocation")
                        self.function_binding_probes[id(parts[0])].append(identifier)
                        self.function_bindings.append({"start_utf16": parts[0].span[0],
                                                       "end_utf16": parts[0].span[1], "point_id": identifier,
                                                       "policy": "preserve-root-function-expression; measure-invocation-only"})
                        continue
                    self.expression_site(parts[0], "binding")
                    continue
                identifier = self.point("line", node.span, kind)
                probes = list(self.body_probes[id(node)]) + [f"{HOOK}.line({identifier})"]
                if kind == "BreakStatement":
                    require(not node.attrs.get("label"), "Labelled break unsupported")
                    owner = ancestor
                    while owner and owner.kind not in {"ForEachStatement", "ForStatement", "WhileStatement", "DoWhileStatement", "SwitchStatement"}:
                        require(not (owner.kind == "TryStatement" and any(part.kind == "Finally" for part in children(owner))),
                                "Break crossing finally needs separate abrupt-completion mapping")
                        owner = parent(owner)
                    if owner and owner.kind == "ForEachStatement":
                        probes.insert(len(self.body_probes[id(node)]), f"{HOOK}.broke({contexts[id(owner)]})")
                if ancestor and ancestor.kind == "StatementList":
                    self.insertions[node.span[0]].extend(self.probe_statement(probe) for probe in probes)
                else:
                    # A block around a single control body retains its execution
                    # condition. Labels/functions-in-single-body are rejected above.
                    require(ancestor and ancestor.kind in {"IfStatement", "ForStatement", "ForEachStatement", "WhileStatement", "DoWhileStatement"},
                            f"Unsupported statement context {kind}/{ancestor.kind if ancestor else 'none'}")
                    self.single_body_probes(node, probes)
        require(self.points, f"No executable AST sites selected in {self.path}")
        # All edits must be properly nested or disjoint. Crossing edits are bugs.
        intervals = sorted(self.wrappers, key=lambda item: (item[0], -item[1], item[4]))
        stack = []
        for interval in intervals:
            while stack and interval[0] >= stack[-1][1]:
                stack.pop()
            require(not stack or interval[1] <= stack[-1][1], "Crossing instrumentation spans")
            stack.append(interval)
        starts, ends = defaultdict(list), defaultdict(list)
        for start, end, before, after, order in self.wrappers:
            starts[start].append((end, order, before))
            ends[end].append((start, order, after))
        offsets = sorted({0, len(self.data) // 2} | set(starts) | set(ends) | set(self.insertions))
        output, recovery, previous = [], [], 0
        for offset in offsets:
            chunk = self.slice((previous, offset))
            output.append(chunk)
            recovery.append(chunk)
            output.extend(item[2] for item in sorted(ends[offset], key=lambda item: (-item[0], -item[1])))
            output.extend(self.insertions[offset])
            output.extend(item[2] for item in sorted(starts[offset], key=lambda item: (-item[0], item[1])))
            previous = offset
        require("".join(recovery) == self.original, "Instrumentation did not preserve every original source byte")
        return "".join(output)


def run_tool(command, ast_dump=False):
    result = subprocess.run(command, text=True, encoding="utf-8", capture_output=True, check=False)
    require(result.returncode == 0, f"Tool failed ({result.returncode}): {command[0]}\n{result.stderr[-3000:]}")
    diagnostics = result.stderr
    if ast_dump:
        # qmldom 1.0 unconditionally qDebug()s this prelude (pinned
        # tools/qmldom/qmldomtool.cpp). Whitelist those exact line forms,
        # retaining all parser/tool warnings as fatal rather than muting logging.
        state = "dirs"
        for line in diagnostics.splitlines():
            line = line.strip()
            if not line:
                continue
            if line == "dirs:" and state == "dirs":
                state = "dir-list"
            elif line.startswith("'") and line.endswith("'") and state in {"dir-list", "file-list"}:
                pass
            elif line == "files:" and state == "dir-list":
                state = "file-list"
            elif line.startswith('fieldFilter:  "') and line.endswith('"') and state == "file-list":
                state = "field-filter"
            elif line == "will load" and state == "field-filter":
                state = "done"
            else:
                raise MappingError(f"Unexpected qmldom diagnostic: {line}")
        require(state == "done", "Missing pinned qmldom diagnostic prelude")
    else:
        require(not diagnostics.strip(), f"Tool diagnostic requires review: {diagnostics[-3000:]}")
    return result.stdout


def read_qrc(root, paths):
    aliases = {}
    for relative in paths:
        qrc = (root / relative).resolve()
        require(qrc.is_relative_to(root), "QRC escapes source root")
        document = ET.parse(qrc).getroot()
        require(document.tag == "RCC", "Expected RCC document")
        for resource in document:
            require(resource.tag == "qresource" and set(resource.attrib) <= {"prefix"}, "Unsupported localized QRC group")
            prefix = resource.get("prefix", "/")
            for element in resource:
                require(element.tag == "file" and set(element.attrib) <= {"alias", "compress", "threshold", "compression-algorithm"},
                        "Unsupported QRC entry")
                require(element.text and element.text.strip(), "Empty QRC source")
                file_path = (qrc.parent / element.text.strip()).resolve()
                require(file_path.is_file() and file_path.is_relative_to(root), "Missing or external QRC input")
                name = str(PurePosixPath("/") / prefix.lstrip("/") / element.get("alias", element.text.strip()))
                require(".." not in PurePosixPath(name).parts and name not in aliases, f"Duplicate/invalid QRC alias {name}")
                aliases[name] = file_path
    require(aliases, "Empty resource selection")
    return aliases


def script_wrapper(text):
    """Parser-only wrapper for function-declaration-only .js libraries.

    No wrapper is executed. Directives are masked with the same UTF-16 length,
    then restored byte-for-byte in the final original .js resource. Other script
    grammars (module imports/exports, top-level state) are explicitly rejected.
    """
    prefix, suffix = "import QtQml\nQtObject {\n", "\n}"
    lines = text.splitlines(keepends=True)
    masked, pragmas, offset = [], [], 0
    for line in lines:
        if line.strip().startswith("."):
            require(line.strip() == ".pragma library" and not pragmas, "Unsupported JavaScript directive")
            replacement = "".join("\n" if char == "\n" else " " for char in line)
            pragmas.append((offset, line))
            masked.append(replacement)
        else:
            masked.append(line)
        offset += len(line)
    return prefix + "".join(masked) + suffix, prefix, suffix, pragmas


def unwrap_script(transformed, tool, original, prefix, suffix, pragmas):
    require(transformed.startswith(prefix) and transformed.endswith(suffix), "Script parser wrapper was modified")
    body = transformed[len(prefix):-len(suffix)]
    shift = len(prefix.encode("utf-16-le")) // 2
    for point in tool.points:
        point["start_utf16"] -= shift
        point["end_utf16"] -= shift
        require(0 <= point["start_utf16"] < point["end_utf16"] <= len(original.encode("utf-16-le")) // 2,
                "Script point escapes original source")
        before = original.encode("utf-16-le")[:2 * point["start_utf16"]].decode("utf-16-le")
        point["line"] = before.count("\n") + 1
        point["column"] = len(before.rsplit("\n", 1)[-1].encode("utf-16-le")) // 2 + 1
        point["coordinate_id"] = f"{tool.path}:{point['start_utf16']}:{point['end_utf16']}:{point['kind']}:{point['role']}"
    first = min(point["start_utf16"] for point in tool.points)
    for offset, line in pragmas:
        require(len(original[:offset + len(line)].encode("utf-16-le")) // 2 <= first,
                "JavaScript directive after executable code unsupported")
        body = body[:offset] + line + body[offset + len(line):]
    return body


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--scope", type=Path, required=True, help='JSON {"files":[{"path":"qml/...", "sha256":"..."}]}')
    parser.add_argument("--resource", action="append", required=True)
    parser.add_argument("--qmldom", required=True)
    parser.add_argument("--qtpaths", required=True, help="Matching pinned Qt installation's qtpaths")
    parser.add_argument("--qmlformat", required=True, help="Parser-only check; formatted stdout is discarded, never written to source")
    parser.add_argument("--rcc", required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--metadata-only", action="append", default=[],
                        help="Explicit reviewed scope path containing only declarative metadata; source SHA is still required")
    args = parser.parse_args()
    root, output = args.source_root.resolve(), args.output_dir.resolve()
    require(not output.exists(), "Output directory already exists; use a fresh directory")
    scope_bytes = args.scope.read_bytes()
    scope = json.loads(scope_bytes)
    require(set(scope) == {"files"} and isinstance(scope["files"], list) and scope["files"], "Empty or invalid explicit scope")
    require(all(isinstance(entry, dict) for entry in scope["files"]), "Invalid explicit scope entry")
    require(len(args.metadata_only) == len(set(args.metadata_only)), "Duplicate metadata-only disposition")
    require(set(args.metadata_only) <= {entry.get("path") for entry in scope["files"]}, "Metadata-only disposition outside scope")
    aliases = read_qrc(root, args.resource)
    version = run_tool([args.qmldom, "--version"]).strip()
    qt_version = run_tool([args.qtpaths, "--qt-version"]).strip()
    require(qt_version == "6.10.2", f"Unqualified Qt version: {qt_version}")
    output.mkdir(parents=True)
    points, sources, instrumented, selected = [], [], {}, set()
    try:
        for entry in scope["files"]:
            require(set(entry) == {"path", "sha256"}, "Each scope entry requires path and original SHA256")
            path = (root / entry["path"]).resolve()
            require(path.is_relative_to(root) and path in aliases.values(), "Scope file is outside selected shipped QRCs")
            require(path.suffix in {".qml", ".js"}, "Scope requires a shipped QML/JS source file")
            require(path not in selected, "Duplicate scope file")
            selected.add(path)
            original_bytes = path.read_bytes()
            require(digest(original_bytes) == entry["sha256"], f"Stale scope source: {entry['path']}")
            text = original_bytes.decode("utf-8")
            require(text.encode("utf-8") == original_bytes and "\r" not in text, "Unsupported source encoding/newline normalization")
            parse_path, parse_text, script = path, text, None
            if path.suffix == ".js":
                script = script_wrapper(text)
                parse_text = script[0]
                parse_path = output / "parser-inputs" / (entry["path"] + ".qml")
                parse_path.parent.mkdir(parents=True, exist_ok=True)
                parse_path.write_text(parse_text, encoding="utf-8")
            run_tool([args.qmlformat, str(parse_path)])
            dump = run_tool([args.qmldom, "--dump-ast", str(parse_path)], ast_dump=True)
            tree = parse_dump(dump)
            if script:
                require(sum(node.kind == "UiObjectDefinition" for node in walk(tree)) == 1,
                        "Only function-declaration JavaScript libraries are supported")
                require(not any(node.kind in {"UiPublicMember", "UiScriptBinding", "UiObjectBinding"} for node in walk(tree)),
                        "Top-level JavaScript state is unsupported")
                for node in walk(tree):
                    if node.kind == "UiSourceElement":
                        require(len(children(node)) == 1 and children(node)[0].kind == "FunctionDeclaration",
                                "Top-level JavaScript must contain only function declarations")
            tool = Instrumenter(entry["path"], parse_text, tree, len(points), entry["path"] in args.metadata_only)
            transformed = tool.run()
            if script:
                transformed = unwrap_script(transformed, tool, text, *script[1:])
            destination = output / "sources" / entry["path"]
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(transformed.encode("utf-8"))
            # Parse transformed code too. Syntax diagnostics are never accepted.
            verify_path = destination
            if script:
                verify_path = output / "parser-outputs" / (entry["path"] + ".qml")
                verify_path.parent.mkdir(parents=True, exist_ok=True)
                verify_path.write_text(script_wrapper(transformed)[0], encoding="utf-8")
            run_tool([args.qmlformat, str(verify_path)])
            parse_dump(run_tool([args.qmldom, "--dump-ast", str(verify_path)], ast_dump=True))
            instrumented[path] = destination
            points.extend(tool.points)
            sources.append({"path": entry["path"], "sha256": digest(original_bytes),
                            "instrumented_sha256": digest(destination.read_bytes()),
                            "length_utf16": len(text.encode("utf-16-le")) // 2,
                            "non_executable_metadata": tool.metadata,
                            "unmeasured_native_initializers": tool.native_initializers,
                            "binding_completion_contexts": tool.completion_blocks,
                            "syntax_preserved_function_bindings": tool.function_bindings,
                            "ast_nodes": dict(sorted(tool.histogram.items())), "point_count": len(tool.points)})
        require(points, "Empty executable point selection")
        require(len({p["coordinate_id"] for p in points}) == len(points), "Duplicate global coverage IDs")
        replacement = ET.Element("RCC", version="1.0")
        group = ET.SubElement(replacement, "qresource", prefix="/")
        manifest_aliases = []
        for alias, original_path in sorted(aliases.items()):
            source = instrumented.get(original_path, original_path)
            element = ET.SubElement(group, "file", alias=alias.lstrip("/"))
            element.text = str(source)
            manifest_aliases.append({"path": ":" + alias, "sha256": digest(source.read_bytes())})
        qrc = output / "replacement.qrc"
        ET.ElementTree(replacement).write(qrc, encoding="utf-8", xml_declaration=True)
        rcc = output / "external.rcc"
        run_tool([args.rcc, "--binary", str(qrc), "--output", str(rcc)])
        manifest = {"schema": 1, "contract": CONTRACT, "mapping_complete": True,
                    "direct_function_binding_policy": "preserve-root-function-expression; declaration-line-site-measures-invocation-only",
                    "tool_qualified": False, "unsupported": [], "scope_sha256": digest(scope_bytes),
                    "explicit_metadata_only": args.metadata_only,
                    "instrumenter_sha256": digest(Path(__file__).read_bytes()),
                    "qmldom_version": version, "qt_version": qt_version,
                    "qmldom_sha256": digest(Path(args.qmldom).read_bytes()),
                    "rcc_sha256": digest(rcc.read_bytes()),
                    "sources": sources, "points": points, "aliases": manifest_aliases,
                    "unmeasured": ["Files outside explicit scope", "Unsupported JavaScript library grammars",
                                   "Direct function-valued QML binding initialization/allocation (invocation only is measured)",
                                   "Textual lines without AST entry sites", "Implicit exception edges",
                                   "Pinned-Qt nested for-in/of and empty try/catch binding completion contexts (rejected)",
                                   "Arrow, nontrailing/multiple, reflective/complex or destructuring parameter defaults (rejected)",
                                   "Native ListElement literal/translation initialization (preserved without JavaScript probes)",
                                   "Qt/native/generated implementation", "Native component internals"]}
        (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        print(json.dumps({"sources": len(sources), "points": len(points), "aliases": len(aliases),
                          "manifest": str(output / "manifest.json"), "tool_qualified": False}))
    except (MappingError, OSError, UnicodeError, ET.ParseError) as error:
        # Failure artifact is explicit. No manifest may certify a partial run.
        (output / "manifest.json").unlink(missing_ok=True)
        (output / "failure.json").write_text(json.dumps({"mapping_complete": False, "error": str(error)}, indent=2) + "\n")
        raise


if __name__ == "__main__":
    try:
        main()
    except (MappingError, OSError, ValueError, ET.ParseError) as exception:
        print(f"QML coverage mapping failed: {exception}", file=sys.stderr)
        sys.exit(1)
