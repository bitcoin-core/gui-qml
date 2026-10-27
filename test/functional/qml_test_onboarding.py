#!/usr/bin/env python3
# Copyright (c) 2024 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test that walks through the full onboarding flow via the test bridge.

Clicks through every onboarding page and verifies the app exits
onboarding into the main view.

This test requires the binary to be built with -DENABLE_TEST_AUTOMATION=ON.
"""

import sys
import time

from qml_test_harness import QmlTestHarness, QmlDriverError, click_onboarding_button, dump_qml_tree, parse_args


def run_tests():
    args = parse_args()
    harness = QmlTestHarness(
        socket_path=args.socket_path,
        reset_settings=not bool(args.socket_path),
        start_onboarded=False,
        use_datadir_arg=bool(args.socket_path),
        extra_args=[] if args.socket_path else ["-regtest"],
    )
    gui = None
    try:
        harness.start()
        gui = harness.driver

        # The app starts fresh, so we should land on the onboarding cover page.
        gui.wait_for_page("onboardingCover", timeout_ms=10000)
        print("Initial page: onboardingCover")

        # Define the expected progression through onboarding.
        # Each entry is (button_to_click, expected_next_page).
        onboarding_steps = [
            ("onboardingCoverButton",           "onboardingStrengthen"),
            ("onboardingStrengthenButton",      "onboardingBlockchain"),
            ("onboardingBlockchainButton",      "onboardingBlockclock"),
            ("onboardingBlockclockButton",      "onboardingStorageLocation"),
            ("onboardingStorageLocationButton", "onboardingStorageAmount"),
            ("onboardingStorageAmountButton",   "onboardingConnection"),
        ]

        for button, expected_page in onboarding_steps:
            print(f"Click {button} ...")
            click_onboarding_button(gui, button)
            gui.wait_for_page(expected_page, timeout_ms=5000)
            print(f"  -> page: {expected_page}")

        # Click Next on the final connection page to finish onboarding.
        print("Click onboardingConnectionButton (finish onboarding) ...")
        click_onboarding_button(gui, "onboardingConnectionButton")

        if not args.socket_path:
            gui = harness.wait_for_main_window_reconnect()

        # Wait until onboarding has actually transitioned out of onboarding
        # pages; this avoids flaky fixed sleeps on slower machines.
        deadline = time.time() + 10
        final_page = gui.get_current_page()
        while "onboarding" in final_page.lower() and time.time() < deadline:
            time.sleep(0.1)
            final_page = gui.get_current_page()

        print(f"  -> post-onboarding page: {final_page}")
        assert "onboarding" not in final_page.lower(), \
            f"Still on an onboarding page after finishing: {final_page}"

        print("\n" + "=" * 50)
        print("All tests PASSED")
        print("=" * 50)

    except Exception as e:
        print(f"\nFAILED: {e}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        if gui is not None:
            dump_qml_tree(gui)
        sys.exit(1)
    finally:
        harness.stop()


if __name__ == '__main__':
    run_tests()
