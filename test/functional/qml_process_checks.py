#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Check GUI process shutdown without hiding warnings or sanitizer failures."""

import re
import subprocess
import sys


_EXPECTED_STDERR = (
    r"This plugin does not support application fonts",
    r'Failed to load font resource: ":/fonts/(?:bitcoincoresans/(?:regular|semibold)|robotomono/regular)"',
    r"(?:qt.core.qobject.connect: )?QObject::connect: No such signal QPlatformNativeInterface::systemTrayWindowChanged\(QScreen\*\)",
    r"Running initialization in thread",
)


def unexpected_gui_stderr(stderr, *, socket_path=None):
    """Accept complete known Qt messages, with optional source locations."""
    expected = list(_EXPECTED_STDERR)
    if socket_path is not None:
        expected.extend((
            r"TestBridge: listening on " + re.escape(socket_path),
            r"TestBridge: client (?:connected|disconnected)",
        ))
    return [
        line.strip()
        for line in stderr.strip().splitlines()
        if line.strip() and not any(
            re.fullmatch(pattern + r"(?: \([^()\n]+:\d+\))?", line.strip())
            for pattern in expected
        )
    ]


def check_gui_exit(returncode, stderr, *, socket_path=None):
    """Require successful exit and no unexpected process diagnostics."""
    assert returncode == 0, f"GUI exited with status {returncode}:\n{stderr}"
    unexpected = unexpected_gui_stderr(stderr, socket_path=socket_path)
    assert not unexpected, "Unexpected GUI stderr:\n" + "\n".join(unexpected)


def report_gui_failure(process, description, *, timeout=5):
    """Collect failure diagnostics before cleanup, without replacing its error."""
    if process is None:
        return
    try:
        # Drain pipes first: a sanitizer report can fill stderr and prevent the
        # process from exiting. Terminating it immediately would cut that report.
        try:
            stdout, stderr = process.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            process.terminate()
            try:
                stdout, stderr = process.communicate(timeout=timeout)
            except subprocess.TimeoutExpired:
                process.kill()
                stdout, stderr = process.communicate(timeout=timeout)
        print(f"GUI failure during {description}; exit status {process.returncode}", file=sys.stderr)
        for name, output in (("stdout", stdout), ("stderr", stderr)):
            if output:
                if isinstance(output, bytes):
                    output = output.decode("utf-8", errors="replace")
                print(f"GUI {name}:\n{output}", file=sys.stderr)
    except Exception as error:
        print(f"Could not collect GUI diagnostics during {description}: {error}", file=sys.stderr)
