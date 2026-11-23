#!/usr/bin/env python3
"""Static analysis for QML-exposed C++ security boundaries.

Scans C++ sources for Q_INVOKABLE methods that accept parameters and lack
obvious input validation/sanitization, which could expose the QML layer to
injection or malformed input.

Usage:
    python3 qml_security_lint.py [directory ...]
"""
import os
import re
import sys

INVOKABLE_PATTERN = re.compile(
    r'Q_INVOKABLE\s+([a-zA-Z0-9_*<>:]+)\s+([a-zA-Z0-9_]+)\s*\((.*?)\)',
    re.MULTILINE | re.DOTALL,
)
VALIDATION_KEYWORDS = [
    'QRegularExpression', 'std::regex', r'if\s*\(', 'validate', 'check',
    'sanitize', 'isValid', 'DecodeDestination', 'IsValidDestination',
    'selectCustomDataDir', 'NormalizeLocalPath', 'ValidateCustomDataDir',
]

# Methods that are thin wrappers around platform/Qt APIs and do not
# introduce an injection boundary of their own.
SAFE_METHODS = {
    'setText', 'text', 'setImage', 'image', 'format', 'clear',
}


def analyze_cpp_file(filepath):
    with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
        content = f.read()

    issues_found = 0
    print(f"[*] Scanning: {filepath}")

    for match in INVOKABLE_PATTERN.finditer(content):
        return_type = match.group(1)
        method_name = match.group(2)
        args = match.group(3)

        if not args.strip() or args.strip() == "void":
            continue

        if method_name in SAFE_METHODS:
            print(f"  [OK] Method '{method_name}' is a known-safe wrapper.")
            continue

        # Heuristic: locate the method body in the same file.
        def_pattern = re.compile(
            rf'(?:\w+::)?{re.escape(method_name)}\s*\([^)]*\)\s*\{{(.*?)\}}',
            re.DOTALL,
        )
        def_match = def_pattern.search(content)

        if def_match:
            body = def_match.group(1)
            has_validation = any(re.search(kw, body) for kw in VALIDATION_KEYWORDS)
            if not has_validation:
                print(f"  [!] WARNING: Q_INVOKABLE method '{method_name}' takes arguments but lacks obvious validation/sanitization patterns.")
                issues_found += 1
            else:
                print(f"  [OK] Method '{method_name}' contains validation checks.")
        else:
            print(f"  [?] NOTE: Declaration found for '{method_name}', but body not in this file (header-only or split implementation).")

    return issues_found


def run_scanner(directories):
    total_issues = 0
    for directory in directories:
        for root, _, files in os.walk(directory):
            for file in files:
                if file.endswith(('.cpp', '.h', '.hpp', '.cc')):
                    filepath = os.path.join(root, file)
                    total_issues += analyze_cpp_file(filepath)

    print("\n==============================")
    print(f"Scan complete. Total potential QML security warnings: {total_issues}")
    return total_issues


def main():
    directories = [d for d in sys.argv[1:] if not d.startswith("--")]
    if not directories:
        directories = ["."]
    total_issues = run_scanner(directories)
    sys.exit(1 if total_issues > 0 else 0)


if __name__ == "__main__":
    main()
