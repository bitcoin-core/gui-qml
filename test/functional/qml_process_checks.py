#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

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
    assert returncode == 0, f"GUI exited with status {returncode}:\n{stderr}"
    unexpected = unexpected_gui_stderr(stderr, socket_path=socket_path)
    assert not unexpected, "Unexpected GUI stderr:\n" + "\n".join(unexpected)


def _drain_pipes_then_stop_process(process, timeout):
    try:
        return process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        process.terminate()
        try:
            return process.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            process.kill()
            return process.communicate(timeout=timeout)


def report_gui_failure(process, description, *, timeout=5):
    if process is None:
        return
    try:
        stdout, stderr = _drain_pipes_then_stop_process(process, timeout)
        print(f"GUI failure during {description}; exit status {process.returncode}", file=sys.stderr)
        for name, output in (("stdout", stdout), ("stderr", stderr)):
            if output:
                if isinstance(output, bytes):
                    output = output.decode("utf-8", errors="replace")
                print(f"GUI {name}:\n{output}", file=sys.stderr)
    except Exception as error:
        print(f"Could not collect GUI diagnostics during {description}: {error}", file=sys.stderr)
