#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Functional test for the URI argument injection mitigation.

Desktop URI handlers pass the URI to the application as a command line
argument, so a crafted bitcoin: URI can append further arguments, and Qt parses
the command line before Bitcoin Core does. See
https://achow101.com/2021/02/0.18-uri-vuln.

Each case appends a Qt option to an otherwise valid command line and asserts
that Core's parser rejects it, which it can only do if Qt left the option
alone. -platformpluginpath, the option used by the original attack, is not
usable as a probe because an unreachable plugin path stalls startup whether or
not Qt read it. Command line URI capture is not implemented yet, so the
injected arguments are passed directly.

This test requires the binary to be built with -DENABLE_TEST_AUTOMATION=ON.
"""

import os
import signal
import subprocess
import sys
import tempfile
import time

from qml_test_harness import (
    QmlTestHarness,
    find_gui_binary,
    qml_qpa_platform,
    qsettings_sandbox_args,
    setup_datadir,
)

# Minimum time to wait for the parser error of a run that must be rejected.
MIN_STARTUP_BUDGET = 15.0

# Multiple of a healthy startup used as the budget for runs that must be rejected.
STARTUP_BUDGET_FACTOR = 3.0


def launch(injected_args):
    """Launch the app with injected_args appended to a valid command line.

    Output goes to a file rather than a pipe so that it can be read while the
    process is still running.
    """
    tmpdir = tempfile.mkdtemp(prefix="qml_test_uri_argument_injection_")
    datadir = setup_datadir(tmpdir)
    socket_path = os.path.join(tmpdir, "test_bridge.sock")
    home_dir = os.path.join(tmpdir, "home")
    os.makedirs(home_dir, exist_ok=True)

    env = dict(os.environ)
    env["QT_QPA_PLATFORM"] = qml_qpa_platform()
    env["HOME"] = home_dir
    settings_args = qsettings_sandbox_args(env, os.path.join(tmpdir, "config"))

    args = [
        find_gui_binary(),
        f"-datadir={datadir}",
        f"-test-automation={socket_path}",
        *settings_args,
        "-qml_onboarded=1",
        "-nolisten",
        *injected_args,
    ]
    print(f"Starting GUI: {' '.join(args)}")
    output_path = os.path.join(tmpdir, "output.txt")
    with open(output_path, "wb") as output_file:
        process = subprocess.Popen(args, env=env, stdout=output_file, stderr=subprocess.STDOUT)
    return process, socket_path, output_path


def read_output(output_path):
    """Return what the process has written so far."""
    with open(output_path, "rb") as output_file:
        return output_file.read().decode("utf-8", errors="replace")


def wait_for_output(output_path, process, needle, budget):
    """Return the output once needle appears, the process exits or time runs out."""
    deadline = time.monotonic() + budget
    while True:
        output = read_output(output_path)
        if needle in output or process.poll() is not None:
            return read_output(output_path)
        if time.monotonic() >= deadline:
            return output
        time.sleep(0.1)


def terminate(process):
    if process.poll() is not None:
        return
    process.send_signal(signal.SIGTERM)
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()


def measure_healthy_startup():
    """Start the app unmodified and return how long the bridge took to appear."""
    harness = QmlTestHarness()
    started = time.monotonic()
    try:
        harness.start()
        elapsed = time.monotonic() - started
        assert harness.driver.get_property("appWindow", "objectName") == "appWindow"
        return elapsed
    finally:
        harness.stop()


def check_argument_reaches_core(injected_args, budget):
    """A Qt option on the command line must be rejected by Bitcoin Core.

    Qt applies the options it recognizes as it parses the command line, so the
    parser error proves the option was left for Bitcoin Core instead.
    """
    option = injected_args[0]
    expected_error = f"Cannot parse command line arguments: Invalid parameter {option}"
    process, socket_path, output_path = launch(injected_args)
    try:
        output = wait_for_output(output_path, process, expected_error, budget)
        assert expected_error in output, (
            f"Core did not report the expected rejection: {expected_error}\n{output}"
        )
        assert not os.path.exists(socket_path), (
            f"The application started with {option} on the command line\n{output}"
        )
    finally:
        terminate(process)


def run_tests():
    try:
        print("\nTest 1: unmodified startup brings up the test bridge")
        elapsed = measure_healthy_startup()
        budget = max(MIN_STARTUP_BUDGET, elapsed * STARTUP_BUDGET_FACTOR)
        print(f"  Bridge came up in {elapsed:.1f}s, budget for rejected runs is {budget:.1f}s")
        print("  PASSED")

        print("\nTest 2: injected -qwindowtitle does not reach Qt")
        check_argument_reaches_core(["-qwindowtitle", "injected"], budget)
        print("  PASSED")

        print("\nTest 3: injected -platform does not reach Qt")
        check_argument_reaches_core(["-platform", "nosuchplatform"], budget)
        print("  PASSED")

        print("\n" + "=" * 50)
        print("All tests PASSED")
        print("=" * 50)

    except Exception as e:
        print(f"\nFAILED: {e}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        sys.exit(1)


if __name__ == '__main__':
    run_tests()
