#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Exercise node runtime dialogs and optionally save screenshots."""

import argparse
import os
import sys
import time

from qml_test_harness import QmlTestHarness, complete_onboarding, dump_qml_tree


ICON_WARNING = 0x00000001
ICON_ERROR = 0x00000002

BTN_OK = 0x00000400
BTN_ABORT = 0x00040000
BTN_RETRY = 0x00080000
BTN_IGNORE = 0x00100000

MODAL = 0x10000000

MSG_WARNING = ICON_WARNING | BTN_OK | MODAL
MSG_ERROR = ICON_ERROR | BTN_OK | MODAL

BUTTON_OBJECT_NAMES = {
    BTN_OK: "nodeRuntimeDialogButtonOk",
    BTN_ABORT: "nodeRuntimeDialogButtonAbort",
    BTN_RETRY: "nodeRuntimeDialogButtonRetry",
    BTN_IGNORE: "nodeRuntimeDialogButtonIgnore",
}

REAL_NODE_DIALOG_CASES = [
    {
        "name": "database-read-error",
        "source": "bitcoin/src/init.cpp coins_error_cb",
        "message": "Error reading from database, shutting down.",
        "style": MSG_ERROR,
        "question": False,
        "buttons": [BTN_OK],
        "dismiss": BTN_OK,
    },
    {
        "name": "deprecated-checkpoints-warning",
        "source": "bitcoin/src/init.cpp -checkpoints warning",
        "message": "Option '-checkpoints' is set but checkpoints were removed. This option has no effect.",
        "style": MSG_WARNING,
        "question": False,
        "buttons": [BTN_OK],
        "dismiss": BTN_OK,
    },
    {
        "name": "reindex-question-ok-abort",
        "source": "bitcoin/src/init.cpp chainstate load failure retry question",
        "message": "Error opening block database.\n\nDo you want to rebuild the databases now?",
        "style": MSG_ERROR | BTN_ABORT,
        "question": True,
        "buttons": [BTN_OK, BTN_ABORT],
        "dismiss": BTN_ABORT,
    },
    {
        "name": "network-options-error",
        "source": "bitcoin/src/net.cpp outgoing connection option conflict",
        "message": "Cannot provide specific connections and have addrman find outgoing connections at the same time.",
        "style": MSG_ERROR,
        "question": False,
        "buttons": [BTN_OK],
        "dismiss": BTN_OK,
    },
    {
        "name": "abort-retry-ignore-button-mask",
        "source": "CClientUIInterface BTN_ABORT | BTN_RETRY | BTN_IGNORE contract sample using a net.cpp error message",
        "message": "Failed to listen on any port. Use -listen=0 if you want this.",
        "style": ICON_ERROR | MODAL | BTN_ABORT | BTN_RETRY | BTN_IGNORE,
        "question": True,
        "buttons": [BTN_ABORT, BTN_RETRY, BTN_IGNORE],
        "dismiss": BTN_ABORT,
    },
]


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--socket-path",
        help="Connect to an already-running bitcoin-core-app test bridge socket.",
    )
    parser.add_argument(
        "--save-screenshots",
        action="store_true",
        help="Save one screenshot for each runtime dialog case.",
    )
    return parser.parse_args()


def make_screenshot_root():
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
    timestamp = time.strftime("%Y%m%d-%H%M%S")
    screenshot_root = os.path.join(repo_root, "test", "artifacts", f"qml_test_node_runtime_dialogs-{timestamp}")
    os.makedirs(screenshot_root, exist_ok=True)
    return screenshot_root


def screenshot_path(root, case_name):
    return os.path.join(root, f"{case_name}.png")


def open_case(gui, case):
    gui.show_runtime_dialog(
        message=case["message"],
        style=case["style"],
        question=case["question"],
    )
    gui.wait_for_property("nodeRuntimeDialog", "opened", True, timeout_ms=5000)
    gui.wait_for_property("nodeRuntimeDialogMessage", "text", case["message"], timeout_ms=5000)
    for button in case["buttons"]:
        object_name = BUTTON_OBJECT_NAMES[button]
        gui.wait_for_object(object_name, timeout_ms=5000)
        gui.wait_for_property(object_name, "visible", True, timeout_ms=5000)


def close_case(gui, case):
    gui.answer_runtime_dialog(case["dismiss"])
    gui.wait_for_property("nodeRuntimeDialog", "opened", False, timeout_ms=5000)


def run_test(*, socket_path=None, save_screenshots=False, screenshot_root=None):
    extra_args = ["-disablewallet", "-qwindowgeometry", "812x665"]
    harness = QmlTestHarness(socket_path=socket_path, extra_args=extra_args)
    try:
        harness.start()
        gui = harness.driver
        if socket_path is None:
            complete_onboarding(gui)
        gui.wait_for_object("nodeRuntimeDialog", timeout_ms=5000)

        for case in REAL_NODE_DIALOG_CASES:
            print(f"[qml_test_node_runtime_dialogs] {case['name']} ({case['source']})")
            open_case(gui, case)
            if save_screenshots:
                path = screenshot_path(screenshot_root, case["name"])
                screenshot = gui.save_screenshot(path)
                print(
                    f"[qml_test_node_runtime_dialogs] screenshot saved to {screenshot['path']} "
                    f"({screenshot['width']}x{screenshot['height']})"
                )
            close_case(gui, case)

        return 0
    except Exception:
        dump_qml_tree(harness.driver) if harness.driver else None
        raise
    finally:
        harness.stop()


def main():
    args = parse_args()
    screenshot_root = make_screenshot_root() if args.save_screenshots else None
    if screenshot_root:
        print(f"Checkpoint screenshots will be saved under: {screenshot_root}")
    return run_test(
        socket_path=args.socket_path,
        save_screenshots=args.save_screenshots,
        screenshot_root=screenshot_root,
    )


if __name__ == "__main__":
    sys.exit(main())
