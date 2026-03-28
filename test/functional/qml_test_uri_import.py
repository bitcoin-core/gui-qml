#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""End-to-end QML URI import test.

Exercises clipboard paste, manual popup entry, malformed URI error feedback,
file import, and drag-drop simulation via the test automation hooks.

Requires:
  - bitcoin-core-app built with -DENABLE_TEST_AUTOMATION=ON
  - Access to bitcoin test_framework (via the bitcoin/ submodule)
"""

import os
import sys
import time
from pathlib import Path
from tempfile import NamedTemporaryFile
from urllib.parse import quote

from qml_test_harness import QmlTestHarness, complete_onboarding, dump_qml_tree, parse_args
from qml_driver import QmlDriverError

REPO_ROOT = Path(__file__).resolve().parents[2]
_BITCOIN_FW_PATH = REPO_ROOT / "bitcoin" / "test" / "functional"
if str(_BITCOIN_FW_PATH) not in sys.path:
    sys.path.insert(0, str(_BITCOIN_FW_PATH))

from test_framework.authproxy import AuthServiceProxy, JSONRPCException

WALLET_NAME = "testwallet"


def _poll_enabled(gui, object_name, timeout=90):
    """Poll get_property until object.enabled is True or timeout is reached."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            if gui.get_property(object_name, "enabled"):
                return
        except Exception:
            pass
        time.sleep(0.5)
    raise TimeoutError(f"Timed out waiting for {object_name!r} to be enabled")


def read_cookie(datadir, timeout=60):
    """Wait for the RPC cookie file and return (user, password)."""
    cookie_path = Path(datadir) / "regtest" / ".cookie"
    deadline = time.time() + timeout
    while not cookie_path.is_file():
        if time.time() > deadline:
            raise TimeoutError(f"RPC cookie not found after {timeout}s: {cookie_path}")
        time.sleep(0.3)
    user, password = cookie_path.read_text(encoding="utf8").strip().split(":", 1)
    return user, password


def make_rpc(datadir, rpc_port=18443, wallet=None):
    """Build an AuthServiceProxy for the GUI node."""
    user, password = read_cookie(datadir)
    url = (
        f"http://{quote(user, safe='')}:{quote(password, safe='')}@127.0.0.1:{rpc_port}"
    )
    if wallet:
        url += f"/wallet/{quote(wallet, safe='')}"
    return AuthServiceProxy(url, timeout=60)


def wait_for_rpc(rpc, timeout=30):
    """Block until the node responds to getblockchaininfo."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            info = rpc.getblockchaininfo()
            if info.get("chain") == "regtest":
                return
        except Exception:
            pass
        time.sleep(0.3)
    raise TimeoutError("Node did not become RPC-ready within timeout")


def create_wallet_via_gui(gui, wallet_name=WALLET_NAME):
    """Drive the wallet creation wizard via the test bridge.

    After complete_onboarding() the app shows the CreateWalletWizard. The node
    initializes concurrently; the RPC cookie appears around the time the wizard
    completes, so we must drive through the wizard before reading the cookie.
    """
    gui.wait_for_page("createWalletWizard", timeout_ms=15000)
    gui.click("createWalletButton")
    gui.wait_for_page("createWalletIntroPage", timeout_ms=5000)
    gui.click("createWalletIntroStartButton")
    gui.wait_for_page("createWalletNamePage", timeout_ms=5000)
    gui.set_text("createWalletNameInput", wallet_name)
    gui.click("createWalletNameContinueButton")
    gui.wait_for_page("createWalletPasswordPage", timeout_ms=5000)
    # The wallet loader is not ready until node initialization completes.
    # Poll get_property in a short loop; wait_for_property would exceed the
    # 30-second socket timeout before the node is ready.
    _poll_enabled(gui, "createWalletPasswordSkipButton", timeout=90)
    # Skip password — creates wallet immediately with no passphrase.
    gui.click("createWalletPasswordSkipButton")
    # Wallet creation happens asynchronously; wait generously for confirmation page.
    gui.wait_for_page("createWalletConfirmPage", timeout_ms=30000)
    gui.click("createWalletConfirmNextButton")
    gui.wait_for_page("createWalletBackupPage", timeout_ms=5000)
    gui.click("createWalletBackupDoneButton")


def navigate_to_send(gui):
    """Click the Send tab and wait for the Send page to appear."""
    gui.click("desktopWalletsSendTab")
    gui.wait_for_page("walletSendPage", timeout_ms=15000)


def open_send_options(gui):
    """Open the Send options (ellipsis) popup."""
    gui.click("sendOptionsButton")
    gui.wait_for_property("sendOptionsPopup", "opened", True, timeout_ms=5000)



def run_tests():
    args = parse_args()
    harness = QmlTestHarness(socket_path=args.socket_path)
    gui = None
    try:
        harness.start()
        gui = harness.driver

        # --- Onboarding ---
        complete_onboarding(gui)
        print("Onboarding complete.")

        # --- Wallet creation wizard ---
        # The app shows CreateWalletWizard after onboarding. Drive through it
        # before trying to connect RPC; the cookie only appears once the node
        # finishes initializing, which coincides with wallet creation completion.
        create_wallet_via_gui(gui)
        print("Wallet creation wizard complete.")

        # --- Node / RPC setup ---
        # Cookie is now available; wallet is already loaded by the GUI.
        rpc = make_rpc(harness.datadir, rpc_port=harness.rpc_port)
        wait_for_rpc(rpc)
        print("Node RPC ready.")

        wallet_rpc = make_rpc(harness.datadir, rpc_port=harness.rpc_port, wallet=WALLET_NAME)
        target_address = wallet_rpc.getnewaddress("uri-test", "bech32")
        print(f"Test address: {target_address}")

        gui.wait_for_property("walletBadge", "noWalletLoaded", False, timeout_ms=10000)
        print("Wallet visible in GUI.")

        navigate_to_send(gui)
        print("Send page open.")

        # ----------------------------------------------------------------
        # Test 1: Clipboard banner — "Fill" button
        # The banner appears automatically when a valid bitcoin: URI is
        # detected in the clipboard. Clicking "Fill" applies the URI fields.
        # ----------------------------------------------------------------
        uri_clip = (
            f"bitcoin:{target_address}"
            f"?amount=0.01234567&label=clip-label&message=clipboard-note"
        )
        gui.set_clipboard_text(uri_clip)
        gui.wait_for_property("clipboardUriBanner", "visible", True, timeout_ms=5000)
        gui.click("clipboardUriPasteButton")
        gui.wait_for_property(
            "sendPaymentRequestStatusText", "text",
            lambda v: "clipboard" in str(v), timeout_ms=10000,
        )
        gui.wait_for_property(
            "sendNoteInput", "text",
            lambda v: "clip-label" in str(v), timeout_ms=5000,
        )
        gui.wait_for_property(
            "sendPaymentRequestMessageText", "text",
            lambda v: "clipboard-note" in str(v), timeout_ms=5000,
        )
        print("Test 1 PASSED: clipboard banner Fill button.")

        # ----------------------------------------------------------------
        # Test 2: Manual URI popup
        # ----------------------------------------------------------------
        uri_manual = (
            f"bitcoin:{target_address}"
            f"?amount=0.02000000&label=manual-label&message=manual-note"
        )
        open_send_options(gui)
        gui.click("sendOptionsOpenPaymentRequestButton")
        gui.wait_for_property("sendUriImportPopup", "opened", True, timeout_ms=5000)
        gui.set_text("sendUriImportInput", uri_manual)
        gui.click("sendUriImportApplyButton")
        gui.wait_for_property("sendUriImportPopup", "opened", False, timeout_ms=5000)
        gui.wait_for_property(
            "sendPaymentRequestStatusText", "text",
            lambda v: "manual entry" in str(v), timeout_ms=10000,
        )
        gui.wait_for_property(
            "sendNoteInput", "text",
            lambda v: "manual-label" in str(v), timeout_ms=5000,
        )
        print("Test 2 PASSED: manual URI popup.")

        # ----------------------------------------------------------------
        # Test 3: Malformed URI shows error (via manual entry popup)
        # A bitcoin:// URI is invalid; the banner never appears for it
        # (parser returns success=False), so the error path is exercised
        # through the "Open payment request" popup instead.
        # ----------------------------------------------------------------
        malformed = f"bitcoin://{target_address}?amount=0.1"
        open_send_options(gui)
        gui.click("sendOptionsOpenPaymentRequestButton")
        gui.wait_for_property("sendUriImportPopup", "opened", True, timeout_ms=5000)
        gui.set_text("sendUriImportInput", malformed)
        gui.click("sendUriImportApplyButton")
        gui.wait_for_property("sendUriImportPopup", "opened", False, timeout_ms=5000)
        gui.wait_for_property(
            "sendPaymentRequestStatusText", "text",
            lambda v: "bitcoin://" in str(v), timeout_ms=10000,
        )
        print("Test 3 PASSED: malformed URI shows error.")

        # ----------------------------------------------------------------
        # Test 4: File import via automation hook
        # ----------------------------------------------------------------
        uri_file = (
            f"bitcoin:{target_address}"
            f"?amount=0.03000000&label=file-label&message=file-note"
        )
        with NamedTemporaryFile("w", suffix=".txt", delete=False, encoding="utf8") as tmp:
            tmp.write(uri_file)
            tmp_path = Path(tmp.name)
        try:
            gui.set_text("sendImportPaymentRequestFilePathInput", str(tmp_path))
            gui.click("sendApplyPaymentRequestFilePathButton")
            gui.wait_for_property(
                "sendPaymentRequestStatusText", "text",
                lambda v: "file" in str(v), timeout_ms=10000,
            )
            gui.wait_for_property(
                "sendNoteInput", "text",
                lambda v: "file-label" in str(v), timeout_ms=5000,
            )
            print("Test 4 PASSED: file import.")
        finally:
            tmp_path.unlink(missing_ok=True)

        # ----------------------------------------------------------------
        # Test 5: Drag-drop simulation via automation hook
        # ----------------------------------------------------------------
        uri_drop = (
            f"bitcoin:{target_address}"
            f"?amount=0.04000000&label=drop-label"
        )
        gui.set_text("sendDropUriInput", uri_drop)
        gui.click("sendApplyDropUriButton")
        gui.wait_for_property(
            "sendPaymentRequestStatusText", "text",
            lambda v: "drag and drop" in str(v), timeout_ms=10000,
        )
        gui.wait_for_property(
            "sendNoteInput", "text",
            lambda v: "drop-label" in str(v), timeout_ms=5000,
        )
        print("Test 5 PASSED: drag-drop simulation.")

        # ----------------------------------------------------------------
        # Test 6: DropArea hasUrls + file:// branch
        # Exercises: strip "file://" prefix → applyPaymentRequestFromFile
        # ----------------------------------------------------------------
        uri_drop_file = (
            f"bitcoin:{target_address}"
            f"?amount=0.05000000&label=drop-file"
        )
        with NamedTemporaryFile("w", suffix=".txt", delete=False, encoding="utf8") as tmp:
            tmp.write(uri_drop_file)
            tmp_path = Path(tmp.name)
        try:
            gui.set_text("sendDropFileUrlInput", tmp_path.as_uri())
            gui.click("sendApplyDropFileUrlButton")
            gui.wait_for_property(
                "sendPaymentRequestStatusText", "text",
                lambda v: "file" in str(v), timeout_ms=10000,
            )
            gui.wait_for_property(
                "sendNoteInput", "text",
                lambda v: "drop-file" in str(v), timeout_ms=5000,
            )
            print("Test 6 PASSED: DropArea hasUrls + file:// branch.")
        finally:
            tmp_path.unlink(missing_ok=True)

        # ----------------------------------------------------------------
        # Test 7: DropArea hasUrls + non-file URL branch
        # Exercises: non-file URL → applyPaymentRequestFromText("drag and drop")
        # ----------------------------------------------------------------
        uri_drop_url = (
            f"bitcoin:{target_address}"
            f"?amount=0.06000000&label=drop-url"
        )
        gui.set_text("sendDropFileUrlInput", uri_drop_url)
        gui.click("sendApplyDropFileUrlButton")
        gui.wait_for_property(
            "sendPaymentRequestStatusText", "text",
            lambda v: "drag and drop" in str(v), timeout_ms=10000,
        )
        gui.wait_for_property(
            "sendNoteInput", "text",
            lambda v: "drop-url" in str(v), timeout_ms=5000,
        )
        print("Test 7 PASSED: DropArea hasUrls + non-file URL branch.")

        print("\n" + "=" * 50)
        print("All URI import tests PASSED (7/7)")
        print("=" * 50)

    except Exception as exc:
        print(f"\nFAILED: {exc}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        if gui is not None:
            dump_qml_tree(gui)
        output = harness.process_output()
        if output:
            print("\n--- GUI process output ---", file=sys.stderr)
            print(output, file=sys.stderr)
        sys.exit(1)
    finally:
        harness.stop()


if __name__ == "__main__":
    run_tests()
