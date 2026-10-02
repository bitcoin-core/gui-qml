#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Keep visible onboarding responsive while its startup work is blocked on I/O."""

import os
from pathlib import Path
import sys
import threading

from qml_process_checks import check_gui_exit
from qml_test_harness import QmlTestHarness, assert_node_shell_visible, walk_onboarding_to_connection


def run_case(action):
    harness = QmlTestHarness(extra_args=["-regtest", "-disablewallet", "-qml_onboarded=0"])
    release = threading.Event()
    reading = threading.Event()
    writer_errors = []
    writer = None
    try:
        harness.start()
        gui = harness.driver
        walk_onboarding_to_connection(gui)

        config = Path(harness.datadir) / "bitcoin.conf"
        contents = config.read_text(encoding="utf8")
        restored_config = config.with_suffix(".ready")
        restored_config.write_text(contents, encoding="utf8")
        config.unlink()
        os.mkfifo(config)

        def serve_config():
            try:
                with config.open("w", encoding="utf8") as stream:
                    reading.set()
                    if not release.wait(30):
                        raise TimeoutError("Startup did not release the blocked config read")
                    # The current read uses the FIFO; subsequent startup reads
                    # use the regular file again.
                    restored_config.replace(config)
                    stream.write(contents)
            except Exception as error:
                writer_errors.append(error)

        writer = threading.Thread(target=serve_config, daemon=True)
        writer.start()
        gui.click("onboardingConnectionButton")
        assert reading.wait(10), "Startup never read the selected data directory's config"
        gui.sock.settimeout(5)
        assert gui.get_property("preInitStartupOverlay", "visible")
        assert gui.get_property("preInitStartupBusyIndicator", "running")
        if action == "quit":
            gui.request_quit()
        elif action == "close":
            gui.close_window()
        assert harness.process.poll() is None, "GUI exited before its worker finished"

        release.set()
        writer.join(timeout=10)
        assert not writer.is_alive(), "Config writer did not finish"
        assert not writer_errors, writer_errors

        if action == "finish":
            gui = harness.wait_for_main_window_reconnect()
            assert_node_shell_visible(gui)
            gui.request_quit()
        harness.process.wait(timeout=30)
        _, stderr = harness.process.communicate(timeout=5)
        check_gui_exit(harness.process.returncode, stderr.decode("utf8", errors="replace"), socket_path=harness.socket_path)
    finally:
        release.set()
        if writer is not None:
            writer.join(timeout=5)
        harness.stop()


if __name__ == "__main__":
    if not hasattr(os, "mkfifo"):
        print("Skipped: blocking config-read fixture requires POSIX FIFOs")
        sys.exit(0)
    for action in ("finish", "quit", "close"):
        run_case(action)
    print("Visible onboarding startup checks passed")
