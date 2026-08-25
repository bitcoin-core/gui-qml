#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the About-page external-link confirmation popup via the test bridge.

Reaches the About page from the onboarding cover (no node-startup wait),
opens the external-link confirmation popup from a link row, and verifies that
cancelling closes it without navigating away. The "Ok" button is never
clicked on purpose: confirming would hand the URL to a real handler and try to
launch a browser. The open success and failure paths, the scheme rejection,
and the copy-URL fallback are covered deterministically by the QML unit test
(test/qml/tst_externalpopup.qml), which drives a stubbed UrlOpener.

This test requires the binary to be built with -DENABLE_TEST_AUTOMATION=ON.
"""

import sys

from qml_test_harness import QmlTestHarness, dump_qml_tree, parse_args

# Each ExternalLink owns its confirmation popup and names it after itself.
WEBSITE_POPUP = "aboutWebsiteLinkIcon_popup"
SOURCE_POPUP = "aboutSourceCodeLinkIcon_popup"


def open_link_popup(gui, link_object_name, popup_object_name):
    """Click a link row and wait for its confirmation popup to open."""
    gui.click(link_object_name)
    gui.wait_for_object(popup_object_name, timeout_ms=5000)
    gui.wait_for_property(popup_object_name, "opened", True, timeout_ms=5000)


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

        # The app starts fresh (-resetguisettings), so we land on the
        # pre-init onboarding cover window. get_current_page() reads the
        # runtime shell's page stack, which does not exist yet in the
        # pre-init window, so wait on the cover page directly.
        print("Wait for the pre-init onboarding cover ...")
        gui.wait_for_page("onboardingCover", timeout_ms=10000)

        # The info button on the cover opens the About page.
        print("Open About page from onboarding cover ...")
        gui.wait_for_object("onboardingCoverInfoButton", timeout_ms=10000)
        gui.click("onboardingCoverInfoButton")
        gui.wait_for_page("settingsAbout", timeout_ms=10000)

        # Opening a link row shows the confirmation popup.
        print("Open confirmation popup from the Website link ...")
        open_link_popup(gui, "aboutWebsiteLink", WEBSITE_POPUP)

        # The popup starts in its confirmation state: both Cancel and Ok are
        # shown, the error/copy fallback is not.
        assert gui.get_property("externalLinkCancel", "visible") is True, \
            "Cancel button should be visible in the confirmation state"
        assert gui.get_property("externalLinkConfirm", "visible") is True, \
            "Ok button should be visible in the confirmation state"

        # Cancelling closes the popup without leaving the About page. The About
        # page is a sub-page of the onboarding cover's internal stack, so check
        # its visibility directly rather than get_current_page(), which reports
        # the outer onboarding page.
        print("Cancel the popup ...")
        gui.click("externalLinkCancel")
        gui.wait_for_property(WEBSITE_POPUP, "opened", False, timeout_ms=5000)
        assert gui.get_property("settingsAbout", "visible") is True, \
            "Expected to remain on the About page after cancelling"

        # The popup is reusable: a second link reopens it and cancels cleanly.
        print("Reopen the popup from the Source code link and cancel ...")
        open_link_popup(gui, "aboutSourceCodeLink", SOURCE_POPUP)
        gui.click("externalLinkCancel")
        gui.wait_for_property(SOURCE_POPUP, "opened", False, timeout_ms=5000)

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
