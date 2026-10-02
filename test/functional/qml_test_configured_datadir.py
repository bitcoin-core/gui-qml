#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Resolve onboarding from a configured profile in an isolated home directory."""

import json
from pathlib import Path
import sys

from qml_process_checks import check_gui_exit
from qml_test_harness import QmlTestHarness, assert_node_shell_visible
from qml_wallet_test_lib import pick_unused_port, rpc_call, wait_for_rpc, write_datadir


def run_case(default_exists, onboarded, override=None):
    harness = QmlTestHarness(
        use_datadir_arg=False,
        start_onboarded=False,
        extra_args=["-regtest", "-disablewallet"],
    )
    try:
        # The harness isolates the child's home. Never inspect or modify the
        # account's real default data directory, even when running the full suite.
        default_suffix = "Library/Application Support/Bitcoin" if sys.platform == "darwin" else ".bitcoin"
        default_datadir = Path(harness.home_dir) / default_suffix
        assert not default_datadir.exists()
        if default_exists:
            default_datadir.mkdir(parents=True)

        configured_datadir = Path(harness.tmpdir) / "configured-profile"
        rpc_port = pick_unused_port()
        write_datadir(configured_datadir, rpc_port, pick_unused_port())
        conf = configured_datadir / "bitcoin.conf"
        conf.write_text(f"datadir={configured_datadir}\n" + conf.read_text(encoding="utf8"), encoding="utf8")
        settings = configured_datadir / "regtest" / "settings.json"
        settings.parent.mkdir(exist_ok=True)
        settings.write_text(json.dumps({"qml_onboarded": onboarded}), encoding="utf8")
        harness.extra_args.append(f"-conf={conf}")
        if override:
            harness.extra_args.append(override)

        harness.start()
        gui = harness.driver
        if not onboarded or override:
            gui.wait_for_page("onboardingCover", timeout_ms=10000)
        else:
            assert_node_shell_visible(gui, timeout_ms=10000)
            assert not gui.object_exists("onboardingCover"), "An onboarded configured profile must skip pre-init onboarding"
            wait_for_rpc(rpc_port)
            assert rpc_call(rpc_port, "getblockchaininfo")["chain"] == "regtest"
        gui.request_quit()
        _, stderr = harness.process.communicate(timeout=30)
        check_gui_exit(harness.process.returncode, stderr.decode("utf8", errors="replace"), socket_path=harness.socket_path)
        assert default_datadir.exists() == default_exists, "Startup must not create the unused default data directory"
    finally:
        try:
            if harness.process and harness.process.poll() is None and harness.driver:
                harness.driver.request_quit()
                harness.process.wait(timeout=30)
        finally:
            harness.stop()


if __name__ == "__main__":
    if sys.platform == "win32":
        print("Skipped: the isolated home fixture requires a Unix default data directory")
        sys.exit(0)
    for default_exists in (False, True):
        for onboarded in (True, False):
            run_case(default_exists, onboarded)
    for override in ("-choosedatadir", "-resetguisettings"):
        run_case(False, True, override)
    print("Configured data directory onboarding checks passed")
