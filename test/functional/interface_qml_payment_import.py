#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Payment import happy paths and command-line argument security."""

import os
import signal
import subprocess
import tempfile
import time
from pathlib import Path
from qml_scenarios import (
    run_scenarios,
    scenario,
)
from qml_smoke_lib import wallet_scenario
from qml_test_harness import (
    QmlTestHarness,
    find_gui_binary,
    qml_qpa_platform,
    qsettings_sandbox_args,
    setup_datadir,
)
from qml_wallet_test_lib import rpc_call


# Minimum time to wait for the parser error of a run that must be rejected.
MIN_STARTUP_BUDGET = 15.0

# Multiple of a healthy startup used as the budget for runs that must be rejected.
STARTUP_BUDGET_FACTOR = 3.0


def launch(injected_args):
    """Launch the app with injected_args appended to a valid command line.

    Output goes to a file rather than a pipe so that it can be read while the
    process is still running.
    """
    tmpdir = tempfile.mkdtemp(prefix="interface_qml_payment_import_")
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


def command_line_security():
    elapsed = measure_healthy_startup()
    budget = max(MIN_STARTUP_BUDGET, elapsed * STARTUP_BUDGET_FACTOR)
    check_argument_reaches_core(["-qwindowtitle", "injected"], budget)
    check_argument_reaches_core(["-platform", "nosuchplatform"], budget)


def uri_smoke(harness, checkpoint):
    """Exercise the user-facing payment-request import happy paths."""
    harness.start_gui()
    gui = harness.driver
    gui.wait_for_property("walletBadge", "loading", False, timeout_ms=30000)
    rpc_call(harness.gui_rpc_port, "createwallet", {"wallet_name": "uri_smoke"})
    gui.wait_for_property("walletBadge", "text", "uri_smoke", timeout_ms=30000)
    address = rpc_call(harness.gui_rpc_port, "getnewaddress", wallet="uri_smoke")
    gui.click("sendTabButton")
    gui.wait_for_page("sendPage")

    gui.set_clipboard_text(f"bitcoin:{address}?amount=0.01&label=Clipboard")
    gui.wait_for_property("clipboardUriPasteButton", "visible", True)
    gui.click("clipboardUriPasteButton")
    gui.wait_for_property("sendPaymentRequestPayTo", "value", "Clipboard")
    checkpoint("clipboard payment request imported")

    gui.click("sendOptionsButton")
    gui.wait_for_property("sendOptionsPopup", "opened", True)
    gui.click("sendOptionsOpenPaymentRequestButton")
    gui.wait_for_property("sendUriImportPopup", "opened", True)
    gui.set_text("sendUriImportInput", f"bitcoin:{address}?amount=0.02&label=Manual")
    gui.click("sendUriImportApplyButton")
    gui.wait_for_property("sendPaymentRequestPayTo", "value", "Manual")
    checkpoint("manual payment request imported")

    path = Path(harness.tmpdir) / "payment request.txt"
    path.write_text(f"bitcoin:{address}?amount=0.03&label=File", encoding="utf8")
    gui.set_text("sendImportPaymentRequestFilePathInput", str(path))
    gui.click("sendApplyPaymentRequestFilePathButton")
    gui.wait_for_property("sendPaymentRequestPayTo", "value", "File")
    checkpoint("payment request file imported")

if __name__ == "__main__":
    raise SystemExit(run_scenarios('Payment import process entry points', [
        wallet_scenario("clipboard-manual-file", 9, uri_smoke),
        scenario("command-line-security", 9, command_line_security),
    ]))
