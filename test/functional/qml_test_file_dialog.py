#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Functional test that a FileDialog can be opened at all.

Every other test that touches the CSV export writes the destination through an
automation field, which skips the dialog. Nothing exercised the path where the
platform offers no native file dialog and QtQuick.Dialogs instantiates its own
QML implementation, which needs Qt.labs.folderlistmodel; without that module
the dialog has no implementation and opening one crashes the process on Qt 6.3
to 6.9.2.

Requires:
  - bitcoin-core-app built with -DENABLE_TEST_AUTOMATION=ON
  - bitcoind built with -DBUILD_DAEMON=ON
"""

import sys
import time

from qml_wallet_test_lib import WalletFlowHarness, rpc_call

WALLET_NAME = "testwallet"


def run_tests():
    harness = WalletFlowHarness("qml_test_file_dialog", port_offset=530)
    try:
        harness.start_gui()
        gui = harness.driver

        gui.wait_for_property("walletBadge", "loading", False, timeout_ms=30000)
        rpc_call(
            harness.gui_rpc_port, "createwallet",
            {"wallet_name": WALLET_NAME, "load_on_startup": True},
        )
        gui.wait_for_property("walletBadge", "text", WALLET_NAME, timeout_ms=30000)

        gui.click("activityTabButton")
        gui.wait_for_property("activityMoreButton", "visible", True, timeout_ms=20000)

        # The automation path field stays empty on purpose: that is what makes
        # the button open the dialog instead of exporting straight away.
        assert gui.get_property("activityExportPathField", "text") == "", (
            "The automation path field must be empty for the dialog to open"
        )
        # A dialog without an implementation takes the process down on open, so
        # the bridge connection drops before any assertion can run. Report that
        # as the crash it is instead of as a connection error.
        try:
            gui.click("activityMoreButton")
            gui.wait_for_property("activityMoreMenu", "opened", True, timeout_ms=5000)
            gui.click("activityExportButton")
            gui.wait_for_property(
                "activityExportDialog", "visible", True, timeout_ms=20000,
            )
        except Exception:
            # The bridge notices the dropped socket before the process has been
            # reaped, so give it a moment before deciding it is still running.
            try:
                harness.gui_process.wait(timeout=10)
            except Exception:
                pass
            if harness.gui_process.poll() is not None:
                raise AssertionError(
                    "The application exited while opening the export dialog, "
                    f"signal or code {harness.gui_process.returncode}. A missing "
                    "Qt.labs.folderlistmodel leaves QtQuick.Dialogs without a "
                    "non-native implementation to instantiate."
                ) from None
            raise
        gui.invoke("activityExportDialog", "close")
        gui.wait_for_property("activityExportDialog", "visible", False, timeout_ms=10000)

        # Still responsive afterwards.
        gui.click("activityTabButton")
        gui.wait_for_property("activityMoreButton", "visible", True, timeout_ms=10000)
        assert harness.gui_process.poll() is None

        print("PASSED: the export dialog opens and closes without taking the app down")

    except Exception as e:
        print(f"\nFAILED: {e}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        sys.exit(1)
    finally:
        harness.stop()


if __name__ == '__main__':
    run_tests()
