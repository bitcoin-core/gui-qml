#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Smoke test for blocksonly node settings."""

import sys

from qml_test_harness import QmlTestHarness, complete_onboarding, dump_qml_tree, parse_args


def run_tests():
    args = parse_args()
    harness = QmlTestHarness(
        socket_path=args.socket_path,
        extra_args=[] if args.socket_path else ["-disablewallet", "-blocksonly"],
    )
    gui = None
    try:
        harness.start()
        gui = harness.driver

        complete_onboarding(gui)
        gui.wait_for_page("nodeSettingsButton", timeout_ms=30000)
        gui.click("nodeSettingsButton")
        gui.wait_for_page("settingsSidebar_about", timeout_ms=10000)

        # The Mempool Information sidebar row is gated on
        # nodeModel.mempoolInformationAvailable, which is false in -blocksonly
        # mode, so the row must be hidden.
        assert not gui.object_exists("settingsSidebar_mempool"), (
            "Mempool Information settings row should not be instantiated in -blocksonly mode"
        )

        print("Blocksonly settings smoke test PASSED")
    except Exception as err:
        print(f"\nFAILED: {err}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        if gui is not None:
            dump_qml_tree(gui)
        sys.exit(1)
    finally:
        harness.stop()


if __name__ == "__main__":
    run_tests()
