#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Functional smoke coverage for the BlockClock page."""

import sys

from qml_test_harness import QmlTestHarness, complete_onboarding, dump_qml_tree, parse_args


def run_tests():
    args = parse_args()
    harness = QmlTestHarness(
        socket_path=args.socket_path,
        extra_args=[] if args.socket_path else ["-disablewallet"],
    )
    gui = None
    try:
        harness.start()
        gui = harness.driver

        complete_onboarding(gui)
        gui.wait_for_page("blockClock", timeout_ms=10000)

        gui.wait_for_property("blockClock", "state", "CONNECTING", timeout_ms=10000)
        assert gui.get_property("blockClock", "header") == "Connecting"
        assert gui.get_property("blockClock", "subText") == "Please wait"

        gui.click("blockClockToggleArea")
        gui.wait_for_property("blockClock", "state", "PAUSE", timeout_ms=5000)
        assert gui.get_property("blockClock", "header") == "Paused"
        assert gui.get_property("blockClock", "subText") == "Tap to resume"

        gui.click("blockClockToggleArea")
        gui.wait_for_property("blockClock", "state", "CONNECTING", timeout_ms=5000)
        assert gui.get_property("blockClock", "header") == "Connecting"
        assert gui.get_property("blockClock", "subText") == "Please wait"

        print("BlockClock smoke test PASSED")
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
