# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Shared process checks and UI actions for short GUI smoke journeys."""

from decimal import Decimal
import os
import time
from pathlib import Path
from qml_driver import QmlDriverError
from qml_process_checks import (
    check_gui_exit,
    report_gui_failure,
)
from qml_test_harness import dump_qml_tree
from qml_wallet_test_lib import (
    WalletFlowHarness,
    open_wallet_selector,
    wait_for_loaded_wallets,
)


def wait_until(predicate, description, timeout=30):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.1)
    raise AssertionError(f"Timed out waiting for {description}")


def select_wallet(gui, wallet_name):
    """Select this catalog identity by physical intent, loading it first if closed."""
    gui.wait_for_property("walletBadge", "loading", False, timeout_ms=20000)
    if gui.get_property("walletBadge", "text") == wallet_name:
        return
    open_wallet_selector(gui)
    selected = {}

    def find_original_row():
        # Opening a closed row moves it between sections. Resolve its canonical
        # name again instead of retaining an obsolete delegate index.
        try:
            for index in range(gui.get_property("walletSelectList", "count")):
                name = gui.get_list_item_property("walletSelectList", index, "name")
                if name == wallet_name:
                    selected["index"] = index
                    selected["state"] = gui.get_list_item_property("walletSelectList", index, "loadState")
                    return True
        except QmlDriverError:
            return False
        return False

    wait_until(find_original_row, "the original wallet picker row", timeout=10)
    # WalletListModel::LoadState is a public typed role: Closed=0, Open=1,
    # Loading=2, LoadError=3. Completion publishes Open but never selects it.
    if selected["state"] == 0:
        gui.click_list_item("walletSelectList", selected["index"])

    def original_opened():
        if not find_original_row():
            return False
        assert selected["state"] != 3, f"Original wallet {wallet_name!r} failed to open"
        return selected["state"] == 1

    wait_until(original_opened, "the original wallet's Open catalog state", timeout=30)
    # This distinct explicit click is required even when this helper started
    # the load: an asynchronous outcome has no selection authority.
    gui.click_list_item("walletSelectList", selected["index"])
    gui.wait_for_property("walletBadge", "text", wallet_name, timeout_ms=10000)
    gui.wait_for_property("walletSelectPopup", "opened", False, timeout_ms=5000)
    gui.wait_for_property("walletSelectPopup", "visible", False, timeout_ms=5000)
    gui.settle(timeout_ms=5000)


def wait_review_send(gui, *, terminal):
    """Wait until the review page's send reaches the named final state."""
    controller = "sendTransactionReviewController"
    def finished():
        observed = gui.get_enum_property(controller, "sendState")
        assert observed["enum"] == "State", observed
        if observed["key"] in ("None", "Pending", "NeedsConfirmation", "NeedsUnlock"):
            return False
        assert observed["key"] == terminal, observed
        return True
    wait_until(original_terminal, "the original " + terminal + " action receipt")


def save_review_psbt(gui, path):
    """Save the review as a PSBT file and wait for its saved receipt."""
    gui.wait_for_property("sendTransactionReviewPage", "detailsReady", True, timeout_ms=30000)
    controller = "sendTransactionReviewController"
    gui.invoke(controller, "beginPsbtSave", [])
    gui.invoke(controller, "choosePsbtSaveFile", [str(path)])
    wait_review_psbt(gui, terminal="Saved")
    assert gui.get_property("sendTransactionReviewPage", "wallet.psbtSavedPath") == str(path)
    assert gui.get_property(controller, "psbtError") in (None, "")


def stop_gui_checked(harness):
    process = harness.gui_process
    harness.stop_gui()
    if process is not None:
        _, stderr = process.communicate()
        check_gui_exit(process.returncode, stderr.decode("utf-8", errors="replace"), socket_path=harness.socket_path)


def wallet_scenario(name, stage, action):
    from qml_scenarios import Scenario
    return Scenario(name, stage, lambda options: run_smoke_case(name, action, options.save_screenshots))


def run_smoke_case(name, scenario, save_screenshots=False):
    harness = WalletFlowHarness(name, port_offset=0)
    artifacts = Path(os.environ.get("QML_TEST_ARTIFACTS", Path(__file__).resolve().parents[2] / "build/test-artifacts")) / name
    index = 0

    def checkpoint(label):
        nonlocal index
        index += 1
        print(f"[{name}] {index}: {label}", flush=True)
        if save_screenshots:
            artifacts.mkdir(parents=True, exist_ok=True)
            harness.driver.save_screenshot(str(artifacts / f"{index:02}.png"))

    try:
        scenario(harness, checkpoint)
        stop_gui_checked(harness)
        print(f"[{name}] PASSED", flush=True)
    except Exception:
        if harness.driver is not None:
            try:
                dump_qml_tree(harness.driver)
            except QmlDriverError:
                pass
        report_gui_failure(harness.gui_process, name)
        raise
    finally:
        harness.stop()


def open_wallet_settings(gui):
    gui.click("desktopWalletSettingsTabButton")
    gui.wait_for_property("desktopWalletSettingsTabButton", "checked", True)
    gui.click("settingsSidebar_wallet")
    gui.wait_for_page("walletSettingsPage")


def close_wallet(harness, wallet_name):
    gui = harness.driver
    open_wallet_selector(gui)
    gui.click(f"walletSelectActions_{wallet_name}")
    gui.click(f"walletSelectClose_{wallet_name}")
    gui.wait_for_property("walletCloseConfirmationPopup", "opened", True)
    assert gui.get_property("walletCloseConfirmationPopup", "targetWallet.name") == wallet_name
    assert gui.get_property("walletCloseConfirmationPopup", "question.wallet.name") == wallet_name
    gui.click("walletCloseConfirmationConfirmButton")
    def original_question_gone():
        try:
            # Never answer or dismiss another wallet's question.
            if gui.get_property("walletCloseConfirmationPopup", "targetWallet.name") != wallet_name:
                return True
            return gui.get_property("walletCloseConfirmationPopup", "visible") is False
        except QmlDriverError as error:
            # The popup may be destroyed between either property read. Its
            # absence is the successful retirement this predicate awaits.
            if "Object not found: walletCloseConfirmationPopup" in str(error):
                return True
            raise
    wait_until(original_question_gone, "the original close question to retire")
    # Popup retirement alone is not a completed wallet close. Require the
    # original public Core unload and the independent GUI catalog projection.
    wait_for_loaded_wallets(harness.gui_rpc_port, [])
    gui.wait_for_property("walletBadge", "noWalletLoaded", True, timeout_ms=30000)


def create_wallet(harness, name, password):
    gui = harness.driver
    harness.open_create_wallet_wizard()
    if gui.object_exists("createWalletButton") and gui.get_property("createWalletButton", "visible"):
        gui.click("createWalletButton")
    gui.wait_for_property("walletTypeRegular", "visible", True)
    gui.click("walletTypeRegular")
    gui.wait_for_page("createWalletFormPage")
    gui.set_text("createWalletNameInput", name)
    gui.set_text("createWalletPasswordInput", password)
    gui.set_text("createWalletPasswordRepeatInput", password)
    gui.click("createWalletPasswordConfirmToggle")
    gui.wait_for_property("createWalletFormCreateButton", "enabled", True)
    gui.click("createWalletFormCreateButton")
    gui.wait_for_page("walletCreationReadyPage", timeout_ms=30000)
    gui.click("createWalletReadyDoneButton")
    gui.wait_for_property("walletBadge", "text", name, timeout_ms=30000)


def import_wallet(harness, backup_path):
    harness.open_create_wallet_wizard()
    gui = harness.driver
    gui.wait_for_property("walletTypeImport", "visible", True)
    gui.set_text("importWalletPathField", str(backup_path))
    gui.click("walletTypeImport")
    gui.wait_for_page("importWalletSuccessPage", timeout_ms=30000)
    gui.click("importWalletSuccessOverviewButton")
    gui.wait_for_property("walletBadge", "text", backup_path.stem, timeout_ms=30000)


def open_receive(gui):
    gui.invoke("walletSelectPopup", "close")
    gui.wait_for_property("walletSelectPopup", "visible", False)
    if gui.object_exists("paymentRequestModal"):
        gui.wait_for_property("paymentRequestModal", "visible", False)
    gui.click("receiveTabButton")
    gui.wait_for_page("receiveWorkspace", timeout_ms=10000)
    gui.wait_for_property("receiveWorkspace", "receive.editor.available", True)
    gui.settle()


def edit_request_field(gui, field, value):
    input_name = {"label": "receivePublicLabelInput", "message": "receiveMessageInput",
                  "note": "receivePrivateNoteInput", "amount": "receiveAmountInput"}[field]
    gui.wait_for_property(input_name, "visible", True)
    gui.wait_for_property(input_name, "readOnly", False)
    gui.set_text(input_name, value)
    gui.invoke(input_name, "editingFinished")


def create_request(gui, amount, label, message, note_self=None):
    """Save the typed original editor, then verify its immutable reopened fields."""
    gui.wait_for_property("receiveWorkspace", "receive.editor.available", True)
    editor_id = gui.get_property("receiveWorkspace", "receive.editor.editorId")
    assert editor_id
    for field, value in (("amount", amount), ("label", label), ("message", message), ("note", note_self)):
        if value:
            edit_request_field(gui, field, value)
    gui.wait_for_property("receiveSaveRequestButton", "enabled", True)
    assert gui.get_property("receiveWorkspace", "receive.editor.editorId") == editor_id
    gui.click("receiveSaveRequestButton")
    gui.wait_for_property("paymentRequestModal", "opened", True)
    gui.wait_for_property("paymentRequestModal", "openedEditor.busy", False)
    assert gui.get_property("paymentRequestModal", "openedEditor.requestId")
    assert gui.get_property("paymentRequestModal", "openedEditor.address")
    expected_satoshi = Decimal(str(amount or "0")) * 100_000_000
    assert expected_satoshi == expected_satoshi.to_integral_value()
    assert gui.get_property("paymentRequestModal", "openedEditor.amount.satoshi") == int(expected_satoshi)
    for path, expected in (("publicLabel.text", label or ""), ("message.text", message or ""),
                           ("privateNote.text", note_self or "")):
        assert gui.get_property("paymentRequestModal", "openedEditor." + path) == expected


def request_qr_payload(gui):
    gui.wait_for_property("paymentRequestModal/savedRequestCopyQrButton", "enabled", True)
    payload = gui.get_property("paymentRequestModal", "card.sharing.paymentUri")
    assert payload and payload == gui.get_property("paymentRequestModal", "openedEditor.paymentUri")
    return payload
