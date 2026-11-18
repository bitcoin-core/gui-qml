#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""End-to-end GUI coverage for the external signer send flow."""

import base64
from decimal import Decimal
import hashlib
import struct
import json
import os
import re
import sys
import time
from datetime import datetime
from qml_driver import QmlDriverError
from qml_process_checks import check_gui_exit
from qml_scenarios import (
    Scenario,
    run_scenarios,
)
from qml_test_harness import dump_qml_tree
from qml_smoke_lib import save_review_psbt, select_wallet
from qml_wallet_test_lib import (
    WalletFlowHarness,
    RPC_USER,
    RPC_PASS,
    rpc_call,
    wait_for_rpc,
)


EXPECTED_SIGNER_NAME = "trezor_t"
EXPECTED_STATUS_TEXT = f"Detected external signer: {EXPECTED_SIGNER_NAME}"
EXPECTED_FIRST_BECH32_ADDRESS = "bcrt1qm90ugl4d48jv8n6e5t9ln6t9zlpm5th68x4f8g"
EXPECTED_FIRST_BECH32M_ADDRESS = "bcrt1phw4cgpt6cd30kz9k4wkpwm872cdvhss29jga2xpmftelhqll62ms4e9sqj"
EXPECTED_FIRST_HD_KEYPATH = "m/84h/1h/0h/0/0"
EXPECTED_FIRST_TAPROOT_HD_KEYPATH = "m/86h/1h/0h/0/0"


class ExternalSignerHarness(WalletFlowHarness):
    def stop_gui(self):
        process = self.gui_process
        super().stop_gui()
        if process is not None:
            _, stderr = process.communicate(timeout=5)
            check_gui_exit(process.returncode, stderr.decode("utf-8", errors="replace"), socket_path=self.socket_path)


def make_screenshot_root():
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
    artifacts_root = os.path.join(repo_root, "test", "artifacts")
    timestamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    screenshot_root = os.path.join(artifacts_root, f"interface_qml_external_signer-{timestamp}")
    os.makedirs(screenshot_root, exist_ok=True)
    return screenshot_root


def find_mock_signer_path(mock_name="signer"):
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
    signer_path = os.path.join(repo_root, "bitcoin", "test", "functional", "mocks", f"{mock_name}.py")
    if not os.path.isfile(signer_path):
        raise FileNotFoundError(f"Mock signer not found at {signer_path}")
    if not os.access(signer_path, os.X_OK):
        raise PermissionError(f"Mock signer is not executable: {signer_path}")
    return signer_path


def exact_signer_config(harness, **changes):
    path = os.path.join(harness.tmpdir, "signer-config.json")
    current = {}
    if os.path.exists(path):
        with open(path, encoding="utf8") as source:
            current = json.load(source)
    current.update(changes)
    temporary = path + ".new"
    with open(temporary, "w", encoding="utf8") as output:
        json.dump(current, output)
    os.replace(temporary, path)
    return path


def create_exact_signer(harness):
    fixture = os.path.join(os.path.dirname(__file__), "mocks", "signer.py")
    config = exact_signer_config(harness, port=harness.gui_rpc_port, user=RPC_USER,
                                password=RPC_PASS, wallet="mock", mode="match",
                                log=os.path.join(harness.tmpdir, "signer-jobs.jsonl"))
    wrapper = os.path.join(harness.tmpdir, "signer.py")
    with open(wrapper, "w", encoding="utf8") as output:
        output.write("#!/usr/bin/env python3\nimport os\nimport sys\n")
        output.write(f"os.execv(sys.executable, [sys.executable, {fixture!r}, '--config', {config!r}, *sys.argv[1:]])\n")
    os.chmod(wrapper, 0o700)
    return wrapper


def decoded_unsigned_transaction(harness, psbt):
    decoded = rpc_call(harness.gui_rpc_port, "decodepsbt", [psbt])
    if "tx" in decoded:
        return decoded["tx"]
    # Core's PSBTv2 RPC exposes terms in the maps, not a v0 global transaction.
    assert decoded["psbt_version"] == 2
    assert all("height_locktime" not in item and "time_locktime" not in item for item in decoded["inputs"])
    tx = {"version": decoded["tx_version"], "locktime": decoded.get("fallback_locktime", 0),
          "vin": [{"txid": item["previous_txid"], "vout": item["previous_vout"],
                   "sequence": item.get("sequence", 0xffffffff)} for item in decoded["inputs"]],
          "vout": [{"value": item["amount"], "scriptPubKey": item["script"]} for item in decoded["outputs"]]}
    # Independent fixture oracle: serialize the small unsigned regtest request.
    assert len(tx["vin"]) < 253 and len(tx["vout"]) < 253
    raw = struct.pack("<iB", tx["version"], len(tx["vin"]))
    for item in tx["vin"]:
        raw += bytes.fromhex(item["txid"])[::-1] + struct.pack("<IBI", item["vout"], 0, item["sequence"])
    raw += bytes([len(tx["vout"])])
    for item in tx["vout"]:
        script = bytes.fromhex(item["scriptPubKey"]["hex"])
        assert len(script) < 253
        amount = Decimal(str(item["value"])) * 100_000_000
        assert amount == int(amount)
        raw += struct.pack("<qB", int(amount), len(script)) + script
    raw += struct.pack("<I", tx["locktime"])
    tx["txid"] = hashlib.sha256(hashlib.sha256(raw).digest()).digest()[::-1].hex()
    return tx


def export_review(harness, name="reviewed"):
    path = os.path.join(harness.tmpdir, name + ".psbt")
    save_review_psbt(harness.driver, path)
    assert os.path.isfile(path), "Review export did not produce a PSBT"
    with open(path, "rb") as source:
        psbt = base64.b64encode(source.read()).decode("ascii")
    return psbt, decoded_unsigned_transaction(harness, psbt)


def matching_signed_review(harness):
    # Keep this validation export independent from the original approval file.
    # Reusing "reviewed.psbt" requires a separate interactive overwrite
    # confirmation and leaves the typed save action pending in this flow.
    psbt, approved = export_review(harness, name="matching")
    processed = rpc_call(harness.gui_rpc_port, "walletprocesspsbt", [psbt, True, "ALL", True, True], wallet="mock")
    assert processed["complete"] is True
    decoded = rpc_call(harness.gui_rpc_port, "decoderawtransaction", [processed["hex"]])
    assert decoded["txid"] == approved["txid"]
    assert rpc_call(harness.gui_rpc_port, "testmempoolaccept", [[processed["hex"]]])[0]["allowed"] is True
    legacy = os.path.join(harness.tmpdir, "mock_psbt")
    if os.path.exists(legacy):
        with open(legacy, encoding="utf8") as source:
            stale = decoded_unsigned_transaction(harness, source.read())
        differing = [field for field in ("version", "locktime", "vin", "vout") if stale[field] != approved[field]]
        print(f"[fixture] independent fixed-response comparison: changed fields={differing}")
    return decoded["txid"]


def substituted_reply(harness, destination, inputs=None):
    options = {"replaceable": True, "feeRate": 0.00001000}
    if inputs is not None:
        options["add_inputs"] = False
    psbt = rpc_call(harness.gui_rpc_port, "walletcreatefundedpsbt",
                    [inputs or [], {destination: 0.75}, 0, options, True], wallet="mock")["psbt"]
    signed = rpc_call(harness.gui_rpc_port, "walletprocesspsbt", [psbt, True, "ALL", True, True], wallet="mock")
    assert signed["complete"] is True
    assert rpc_call(harness.gui_rpc_port, "testmempoolaccept", [[signed["hex"]]])[0]["allowed"] is True
    return signed["psbt"], rpc_call(harness.gui_rpc_port, "decoderawtransaction", [signed["hex"]])


class CheckpointRecorder:
    def __init__(self, case_name, save_screenshots, screenshot_root):
        self.case_name = case_name
        self.save_screenshots = save_screenshots
        self.screenshot_root = screenshot_root
        self.index = 0

    def _sanitize_label(self, label):
        return re.sub(r"[^a-z0-9]+", "-", label.lower()).strip("-") or "checkpoint"

    def checkpoint(self, label, gui=None):
        self.index += 1
        prefix = f"[{self.case_name}] checkpoint {self.index:02d}"
        print(f"{prefix}: {label}")
        if gui is None:
            return

        gui.settle()

        if not self.save_screenshots:
            return

        case_dir = os.path.join(self.screenshot_root, self.case_name)
        os.makedirs(case_dir, exist_ok=True)
        filename = f"{self.index:02d}-{self._sanitize_label(label)}.png"
        screenshot_path = os.path.join(case_dir, filename)
        screenshot = gui.save_screenshot(screenshot_path)
        print(
            f"{prefix}: screenshot saved to {screenshot['path']} "
            f"({screenshot['width']}x{screenshot['height']})"
        )


def wait_until(predicate, timeout=20, interval=0.1, description="condition"):
    deadline = time.time() + timeout
    last_error = None
    while time.time() < deadline:
        try:
            if predicate():
                return
        except Exception as err:  # noqa: BLE001 - test polling should tolerate transient state
            last_error = err
        time.sleep(interval)
    if last_error:
        raise AssertionError(f"Timed out waiting for {description}: {last_error}")
    raise AssertionError(f"Timed out waiting for {description}")


def expect_runtime_error(callable_obj, expected_substring):
    try:
        callable_obj()
    except RuntimeError as err:
        assert expected_substring in str(err), (
            f"Expected error containing {expected_substring!r}, got {err!r}"
        )
        return
    raise AssertionError(f"Expected RuntimeError containing {expected_substring!r}")


def create_wallet(port, name, *, disable_private_keys=False, blank=False, load_on_startup=False, external_signer=False):
    return rpc_call(
        port,
        "createwallet",
        [
            name,
            disable_private_keys,
            blank,
            "",
            False,
            True,
            load_on_startup,
            external_signer,
        ],
    )


def load_settings(settings_path):
    with open(settings_path, "r", encoding="utf8") as settings_file:
        return json.load(settings_file)


def wait_for_wallet(gui, wallet_name):
    gui.wait_for_property("walletBadge", "text", wallet_name, timeout_ms=20000)
    gui.settle(timeout_ms=5000)


def wait_for_wallet_balance(port, wallet_name):
    wait_until(
        lambda: float(rpc_call(port, "getbalance", wallet=wallet_name)) > 0,
        timeout=30,
        description=f"{wallet_name} balance",
    )


def wait_for_wallet_badge_balance(gui, expected_satoshis):
    wait_until(
        lambda: gui.get_property("walletBadge", "balanceSatoshi") == expected_satoshis,
        timeout=30,
        description=f"wallet badge balance {expected_satoshis} satoshis",
    )


def open_add_wallet_flow(gui):
    gui.wait_for_property("walletBadge", "loading", False, timeout_ms=20000)
    gui.click("walletBadge")
    try:
        gui.wait_for_property("walletSelectPopup", "opened", True, timeout_ms=2000)
        gui.click("walletSelectAddWalletButton")
    except QmlDriverError:
        pass
    gui.wait_for_property("walletTypeExternalSigner", "visible", True, timeout_ms=10000)


def ensure_desktop_wallets_visible(gui):
    try:
        gui.wait_for_property("desktopWalletSettingsTabButton", "visible", True, timeout_ms=1000)
        return
    except QmlDriverError:
        pass

    gui.wait_for_property("onboardingCloseButton", "visible", True, timeout_ms=10000)
    gui.click("onboardingCloseButton")
    gui.wait_for_property("desktopWalletSettingsTabButton", "visible", True, timeout_ms=10000)


def open_wallet_settings(gui):
    ensure_desktop_wallets_visible(gui)
    gui.click("desktopWalletSettingsTabButton")
    # External-signer configuration lives in its own redesigned sidebar page.
    gui.wait_for_property("settingsSidebar_external-signer", "visible", True, timeout_ms=10000)
    gui.click("settingsSidebar_external-signer")
    gui.wait_for_property("externalSignerPathInput", "visible", True, timeout_ms=10000)


def open_selected_wallet_settings(gui):
    ensure_desktop_wallets_visible(gui)
    gui.click("desktopWalletSettingsTabButton")
    # Per-wallet settings live under the sidebar Wallet section.
    gui.wait_for_property("settingsSidebar_wallet", "visible", True, timeout_ms=10000)
    gui.click("settingsSidebar_wallet")
    try:
        gui.wait_for_page("walletSettingsPage", timeout_ms=1000)
    except QmlDriverError:
        # If a prior step left the preserved wallet stack on a subpage, return
        # to the redesigned wallet settings root.
        for back_button in (
            "walletPasswordBackButton",
            "addressListBackButton",
            "signVerifyMessageBackButton",
        ):
            try:
                if gui.get_property(back_button, "visible") is True:
                    gui.click(back_button)
                    break
            except QmlDriverError:
                pass
        gui.wait_for_page("walletSettingsPage", timeout_ms=10000)


def configure_external_signer_via_gui(harness, checkpoints, signer_path):
    gui = harness.driver
    open_wallet_settings(gui)
    checkpoints.checkpoint("wallet settings opened", gui)

    gui.set_text("externalSignerPathInput", signer_path)
    checkpoints.checkpoint("mock signer path populated", gui)

    gui.click("externalSignerCheckDeviceButton")
    gui.wait_for_property("externalSignerStatusText", "text", EXPECTED_STATUS_TEXT, timeout_ms=10000)
    wait_until(
        lambda: os.path.exists(harness.gui_settings_path),
        description="settings.json creation",
    )
    wait_until(
        lambda: load_settings(harness.gui_settings_path).get("signer") == signer_path,
        description="signer setting persistence",
    )
    checkpoints.checkpoint("mock signer detected", gui)
    print(f"[{checkpoints.case_name}] signer status: {gui.get_text('externalSignerStatusText')}")


def create_and_verify_external_wallet(harness, checkpoints):
    gui = harness.driver
    open_add_wallet_flow(gui)
    checkpoints.checkpoint("add wallet flow opened", gui)

    gui.wait_for_property("walletTypeExternalSigner", "visible", True, timeout_ms=10000)
    checkpoints.checkpoint("external wallet option available", gui)

    gui.click("walletTypeExternalSigner")
    gui.wait_for_property("externalWalletNameInput", "visible", True, timeout_ms=10000)
    gui.wait_for_property("createExternalWalletButton", "enabled", True, timeout_ms=10000)
    checkpoints.checkpoint("external wallet form opened", gui)

    wallet_name = gui.get_text("externalWalletNameInput")
    print(f"[{checkpoints.case_name}] suggested wallet name: {wallet_name}")
    assert wallet_name == EXPECTED_SIGNER_NAME, (
        f"Expected wallet name to default to {EXPECTED_SIGNER_NAME!r}, got {wallet_name!r}"
    )
    checkpoints.checkpoint("wallet name prefilled from signer", gui)

    gui.click("createExternalWalletButton")
    gui.wait_for_page("externalWalletCreatedPage", timeout_ms=20000)
    checkpoints.checkpoint("external wallet created", gui)

    gui.click("externalWalletCreatedDoneButton")
    gui.wait_for_property("walletBadge", "text", wallet_name, timeout_ms=20000)
    checkpoints.checkpoint("wallet overview reopened", gui)

    wallet_dir = os.path.join(harness.gui_wallets_path, wallet_name)
    assert os.path.isdir(wallet_dir), f"Expected wallet directory at {wallet_dir}"
    print(f"[{checkpoints.case_name}] wallet directory: {wallet_dir}")

    wallet_info = rpc_call(harness.gui_rpc_port, "getwalletinfo", wallet=wallet_name)
    print(f"[{checkpoints.case_name}] wallet info: {json.dumps(wallet_info, sort_keys=True)}")
    assert wallet_info["external_signer"] is True, "Expected external_signer wallet flag"
    assert wallet_info["private_keys_enabled"] is False, "External signer wallet should disable private keys"
    assert wallet_info["descriptors"] is True, "External signer wallet should be descriptor based"
    assert wallet_info["format"] == "sqlite", "External signer wallet should use sqlite storage"

    receive_address = rpc_call(
        harness.gui_rpc_port,
        "getnewaddress",
        ["", "bech32"],
        wallet=wallet_name,
    )
    print(f"[{checkpoints.case_name}] first bech32 address: {receive_address}")
    assert receive_address == EXPECTED_FIRST_BECH32_ADDRESS, (
        "Expected upstream mock signer to provide the same first bech32 address "
        "used in bitcoin/test/functional/wallet_signer.py"
    )

    address_info = rpc_call(harness.gui_rpc_port, "getaddressinfo", [receive_address], wallet=wallet_name)
    print(f"[{checkpoints.case_name}] address info: {json.dumps(address_info, sort_keys=True)}")
    assert address_info["solvable"] is True, "Expected signer-derived address to be solvable"
    assert address_info["ismine"] is True, "Expected signer-derived address to be recognized as mine"
    assert address_info["hdkeypath"] == EXPECTED_FIRST_HD_KEYPATH, (
        f"Expected first signer-derived address hdkeypath {EXPECTED_FIRST_HD_KEYPATH}, "
        f"got {address_info['hdkeypath']}"
    )
    checkpoints.checkpoint("external wallet rpc descriptors verified", gui)
    return wallet_name


def setup_mock_wallet(gui_rpc_port):
    create_wallet(gui_rpc_port, "mock", blank=True, load_on_startup=False)
    descriptors = [
        {
            "desc": "tr([00000001/86h/1h/0']tprv8ZgxMBicQKsPd7Uf69XL1XwhmjHopUGep8GuEiJDZmbQz6o58LninorQAfcKZWARbtRtfnLcJ5MQ2AtHcQJCCRUcMRvmDUjyEmNUWwx8UbK/0/*)#7ew68cn8",
            "timestamp": 0,
            "range": [0, 1],
            "internal": False,
            "active": True,
        },
        {
            "desc": "tr([00000001/86h/1h/0']tprv8ZgxMBicQKsPd7Uf69XL1XwhmjHopUGep8GuEiJDZmbQz6o58LninorQAfcKZWARbtRtfnLcJ5MQ2AtHcQJCCRUcMRvmDUjyEmNUWwx8UbK/1/*)#0dtm6drl",
            "timestamp": 0,
            "range": [0, 0],
            "internal": True,
            "active": True,
        },
    ]
    result = rpc_call(gui_rpc_port, "importdescriptors", [descriptors], wallet="mock")
    assert result[0]["success"] is True
    assert result[1]["success"] is True


def prepare_signed_mock_psbt(harness, external_wallet_name, destination_address):
    receive_address = rpc_call(
        harness.gui_rpc_port,
        "getnewaddress",
        ["", "bech32m"],
        wallet=external_wallet_name,
    )
    print(f"[qml_external_signer] first bech32m address: {receive_address}")
    assert receive_address == EXPECTED_FIRST_BECH32M_ADDRESS, (
        "Expected upstream mock signer to provide the same first bech32m address "
        "used in bitcoin/test/functional/wallet_signer.py"
    )
    address_info = rpc_call(harness.gui_rpc_port, "getaddressinfo", [receive_address], wallet=external_wallet_name)
    assert address_info["hdkeypath"] == EXPECTED_FIRST_TAPROOT_HD_KEYPATH, (
        f"Expected first taproot signer-derived address hdkeypath {EXPECTED_FIRST_TAPROOT_HD_KEYPATH}, "
        f"got {address_info['hdkeypath']}"
    )

    rpc_call(harness.gui_rpc_port, "generatetoaddress", [101, receive_address])

    mock_psbt = rpc_call(
        harness.gui_rpc_port,
        "walletcreatefundedpsbt",
        [[], {destination_address: 0.5}, 0, {"replaceable": True, "feeRate": 0.00001000}, True],
        wallet="mock",
    )["psbt"]
    processed = rpc_call(
        harness.gui_rpc_port,
        "walletprocesspsbt",
        [mock_psbt, True, "ALL", True, True],
        wallet="mock",
    )
    mock_tx = processed["hex"]
    assert rpc_call(harness.gui_rpc_port, "testmempoolaccept", [[mock_tx]])[0]["allowed"] is True

    mock_psbt_path = os.path.join(harness.tmpdir, "mock_psbt")
    with open(mock_psbt_path, "w", encoding="utf8") as mock_psbt_file:
        mock_psbt_file.write(processed["psbt"])

    return rpc_call(harness.gui_rpc_port, "decoderawtransaction", [mock_tx])["txid"]


def set_multiple_recipients(gui, enabled):
    if enabled:
        if not gui.object_exists("sendRecipientCard_1"):
            gui.click("sendAddRecipientButton")
    else:
        while gui.object_exists("sendRecipientCard_1"):
            gui.click("sendRemoveRecipient_1")
    gui.settle()


def set_amount_unit(gui, unit_label):
    current = gui.get_property("sendAmountUnitLabel", "text")
    if current != unit_label:
        gui.click("sendAmountUnitToggle")
        gui.wait_for_property("sendAmountUnitLabel", "text", unit_label, timeout_ms=5000)


def open_external_signer_review(gui, destination_address, amount_text):
    gui.click("sendTabButton")
    gui.wait_for_page("sendPage", timeout_ms=10000)
    set_multiple_recipients(gui, False)
    set_amount_unit(gui, "BTC")
    gui.set_text("sendAddressInput", destination_address)
    gui.set_text("sendAmountInput", amount_text)
    gui.wait_for_property("sendReviewButton", "enabled", True, timeout_ms=10000)
    gui.click("sendReviewButton")
    gui.wait_for_page("sendTransactionReviewPage", timeout_ms=10000)


def assert_signer_status(gui, object_name, expected_text):
    wait_until(
        lambda: gui.get_text(object_name) == expected_text,
        description=f"{object_name} text",
    )


def fail_case(case_name, harness, checkpoints, err):
    print(f"\nFAILED [{case_name}]: {err}", file=sys.stderr)
    import traceback
    traceback.print_exc()
    if harness.driver is not None:
        try:
            checkpoints.checkpoint("failure state", harness.driver)
        except Exception as screenshot_err:  # noqa: BLE001 - preserve original failure context
            print(f"[{case_name}] failed to save failure screenshot: {screenshot_err}", file=sys.stderr)
    gui_output = harness.process_output(harness.gui_process)
    if gui_output:
        print("\n--- GUI process output ---", file=sys.stderr)
        print(gui_output, file=sys.stderr)
    if harness.driver is not None:
        dump_qml_tree(harness.driver)


def run_test(args):
    case_name = "qml_external_signer"
    screenshot_root = None
    if args.save_screenshots:
        screenshot_root = make_screenshot_root()
        print(f"Checkpoint screenshots will be saved under: {screenshot_root}")

    harness = ExternalSignerHarness(case_name, port_offset=90)
    checkpoints = CheckpointRecorder(case_name, args.save_screenshots, screenshot_root)
    try:
        signer_path = create_exact_signer(harness)
        no_signer_path = find_mock_signer_path("no_signer")

        print(f"[{case_name}] starting")
        harness.start_gui(cwd=harness.tmpdir)
        checkpoints.checkpoint("GUI launched", harness.driver)
        harness.finish_onboarding()
        wait_for_rpc(harness.gui_rpc_port)
        checkpoints.checkpoint("onboarding completed", harness.driver)

        expect_runtime_error(
            lambda: rpc_call(harness.gui_rpc_port, "enumeratesigners"),
            "restart bitcoind with -signer=<cmd>",
        )
        expect_runtime_error(
            lambda: create_wallet(harness.gui_rpc_port, "missing_signer", disable_private_keys=True, external_signer=True),
            "restart bitcoind with -signer=<cmd>",
        )
        checkpoints.checkpoint("missing signer setting rejected", harness.driver)

        configure_external_signer_via_gui(harness, checkpoints, signer_path)
        wallet_name = create_and_verify_external_wallet(harness, checkpoints)
        open_selected_wallet_settings(harness.driver)
        harness.driver.wait_for_property("walletPasswordRow", "visible", False, timeout_ms=10000)
        harness.driver.wait_for_property("walletBackupRow", "visible", True, timeout_ms=10000)
        checkpoints.checkpoint("external signer wallet hides password settings", harness.driver)

        create_wallet(harness.gui_rpc_port, "miner", load_on_startup=False)
        setup_mock_wallet(harness.gui_rpc_port)

        destination_address = rpc_call(harness.gui_rpc_port, "getnewaddress", wallet="miner")
        expected_txid = prepare_signed_mock_psbt(harness, wallet_name, destination_address)
        checkpoints.checkpoint("signed mock PSBT prepared", harness.driver)
        select_wallet(harness.driver, wallet_name)
        checkpoints.checkpoint("external signer wallet reselected", harness.driver)
        wait_for_wallet_badge_balance(harness.driver, 5_000_000_000)
        checkpoints.checkpoint("wallet badge balance updated", harness.driver)

        harness.restart_gui(cwd=harness.tmpdir)
        wait_for_rpc(harness.gui_rpc_port)
        signers = rpc_call(harness.gui_rpc_port, "enumeratesigners")["signers"]
        assert {"fingerprint": "00000001", "name": "trezor_t"} in signers
        wait_for_wallet(harness.driver, wallet_name)
        wait_for_wallet_balance(harness.gui_rpc_port, wallet_name)
        wait_for_wallet_badge_balance(harness.driver, 5_000_000_000)
        checkpoints.checkpoint("wallet badge balance restored after restart", harness.driver)

        harness.update_gui_settings({"signer": no_signer_path})
        harness.restart_gui(cwd=harness.tmpdir)
        wait_for_rpc(harness.gui_rpc_port)
        assert rpc_call(harness.gui_rpc_port, "enumeratesigners")["signers"] == []
        wait_for_wallet(harness.driver, wallet_name)
        wait_for_wallet_balance(harness.gui_rpc_port, wallet_name)
        wait_for_wallet_badge_balance(harness.driver, 5_000_000_000)
        checkpoints.checkpoint("wallet badge balance confirmed without signer", harness.driver)

        open_external_signer_review(harness.driver, destination_address, "0.50000000")
        checkpoints.checkpoint("no-signer review displayed", harness.driver)
        assert_signer_status(
            harness.driver,
            "sendTransactionReviewSignerStatus",
            "Approve on external signer to broadcast this transaction.",
        )
        harness.driver.click("sendTransactionReviewSignerButton")
        assert_signer_status(
            harness.driver,
            "sendTransactionReviewSignerStatus",
            "External signer not found. Connect one device and try again.",
        )
        checkpoints.checkpoint("no-signer review error surfaced", harness.driver)

        assert harness.driver.get_property("transactionFlowInput_0", "visible")
        harness.update_gui_settings({"signer": signer_path})
        harness.restart_gui(cwd=harness.tmpdir)
        wait_for_rpc(harness.gui_rpc_port)
        signers = rpc_call(harness.gui_rpc_port, "enumeratesigners")["signers"]
        assert {"fingerprint": "00000001", "name": "trezor_t"} in signers
        wait_for_wallet(harness.driver, wallet_name)
        wait_for_wallet_balance(harness.gui_rpc_port, wallet_name)
        wait_for_wallet_badge_balance(harness.driver, 5_000_000_000)
        checkpoints.checkpoint("wallet badge balance confirmed after signer restore", harness.driver)
        if "mock" not in rpc_call(harness.gui_rpc_port, "listwallets"):
            rpc_call(harness.gui_rpc_port, "loadwallet", ["mock", False])
        select_wallet(harness.driver, wallet_name)

        open_external_signer_review(harness.driver, destination_address, "0.50000000")
        checkpoints.checkpoint("signer review displayed", harness.driver)
        expected_txid = matching_signed_review(harness)
        harness.driver.click("sendTransactionReviewSignerButton")
        assert_signer_status(
            harness.driver,
            "sendTransactionReviewSignerStatus",
            "Signed on external signer. Ready to send.",
        )
        checkpoints.checkpoint("signer approval completed", harness.driver)
        harness.driver.click("sendTransactionReviewSignerButton")
        wait_until(
            lambda: expected_txid in rpc_call(harness.gui_rpc_port, "getrawmempool"),
            description="signed transaction broadcast",
        )
        checkpoints.checkpoint("signed transaction broadcast", harness.driver)

        print(f"[{case_name}] completed")
        print("External signer GUI flow passed.")
        return 0
    except Exception as err:  # noqa: BLE001 - preserve failure context for functional test output
        fail_case(case_name, harness, checkpoints, err)
        return 1
    finally:
        harness.stop()


def substitution_journey(args, replacement):
    case_name = "signer-substitution-speed-up" if replacement else "signer-substitution-send"
    harness = ExternalSignerHarness(case_name, port_offset=94 if replacement else 92)
    checkpoints = CheckpointRecorder(case_name, False, None)
    try:
        signer = create_exact_signer(harness)
        harness.start_gui(cwd=harness.tmpdir)
        harness.finish_onboarding()
        wait_for_rpc(harness.gui_rpc_port)
        configure_external_signer_via_gui(harness, checkpoints, signer)
        wallet_name = create_and_verify_external_wallet(harness, checkpoints)
        create_wallet(harness.gui_rpc_port, "miner", load_on_startup=False)
        setup_mock_wallet(harness.gui_rpc_port)
        intended = rpc_call(harness.gui_rpc_port, "getnewaddress", wallet="miner")
        other = rpc_call(harness.gui_rpc_port, "getnewaddress", wallet="miner")
        prepare_signed_mock_psbt(harness, wallet_name, intended)
        # Two independent mature coins make a genuinely disjoint replacement possible.
        mining = rpc_call(harness.gui_rpc_port, "getnewaddress", ["", "bech32m"], wallet=wallet_name)
        rpc_call(harness.gui_rpc_port, "generatetoaddress", [2, mining])
        select_wallet(harness.driver, wallet_name)
        wait_for_wallet_balance(harness.gui_rpc_port, wallet_name)
        open_external_signer_review(harness.driver, intended, "0.50000000")
        _, approved = export_review(harness)
        expected = matching_signed_review(harness)
        original_inputs = {(item["txid"], item["vout"]) for item in approved["vin"]}
        if replacement:
            harness.driver.click("sendTransactionReviewSignerButton")
            assert_signer_status(harness.driver, "sendTransactionReviewSignerStatus", "Signed on external signer. Ready to send.")
            harness.driver.click("sendTransactionReviewSignerButton")
            wait_until(lambda: expected in rpc_call(harness.gui_rpc_port, "getrawmempool"), description="original payment")
            coins = rpc_call(harness.gui_rpc_port, "listunspent", [1], wallet="mock")
            independent = next(coin for coin in coins if (coin["txid"], coin["vout"]) not in original_inputs)
            reply, counterfeit = substituted_reply(harness, other, [{"txid": independent["txid"], "vout": independent["vout"]}])
            assert original_inputs.isdisjoint({(item["txid"], item["vout"]) for item in counterfeit["vin"]})
            exact_signer_config(harness, mode="reply", psbt=reply)
            gui = harness.driver
            gui.click("activityTabButton")
            gui.set_text("activitySearchField", expected)
            gui.wait_for_property("activityFilterProxyModel", "busy", False, timeout_ms=30000)
            gui.wait_for_property("activityFilterProxyModel", "count", 1, timeout_ms=30000)
            gui.wait_for_property("activityListView", "count", 1, timeout_ms=30000)
            def matches_filtered_txid():
                return gui.get_model_row("activityListView", "model", row=0, role="txid",
                                         expected_rows=1)["value"] == expected
            try:
                wait_until(matches_filtered_txid, timeout=30, description="filtered Activity model identity")
            except AssertionError:
                print("[diagnostic] expected Activity row:", expected)
                print("[diagnostic] actual Activity row:", gui.get_model_row(
                    "activityListView", "model", row=0, role="txid", expected_rows=1)["value"])
                print("[diagnostic] filter text:", gui.get_property("activityFilterProxyModel", "searchText"))
                print("[diagnostic] filter count:", gui.get_property("activityFilterProxyModel", "count"))
                raise
            gui.click_list_item("activityListView", 0, "activityRowOpenButton")
            gui.wait_for_page("activityDetailsPage")
            gui.wait_for_property("activityDetailsPage", "transactionId", expected, timeout_ms=30000)
            gui.wait_for_property("activityDetailsPage", "detailsLoading", False, timeout_ms=30000)
            gui.wait_for_property("activityDetailsPage", "canBump", True, timeout_ms=30000)
            gui.click("speedUpBannerPrimaryButton")
            gui.wait_for_property("speedUpOverlay", "opened", True)
            try:
                gui.wait_for_property("updateTransactionButton", "enabled", True, timeout_ms=30000)
            except QmlDriverError:
                print("[diagnostic] speed-up state:", gui.get_property("speedUpOverlay", "bumpState"))
                print("[diagnostic] speed-up error:", gui.get_property("speedUpOverlay", "bumpErrorText"))
                raise
            gui.click("updateTransactionButton")
            gui.wait_for_property("speedUpErrorText", "text", "Signer changed the reviewed replacement transaction.")
            assert set(rpc_call(harness.gui_rpc_port, "getrawmempool")) == {expected}
            original = rpc_call(harness.gui_rpc_port, "gettransaction", [expected], wallet=wallet_name)
            assert not original.get("replaced_by_txid")
            assert counterfeit["txid"] not in original["walletconflicts"]
            print("[oracle] disjoint independently valid signer response rejected; original remains unreplaced")
            # A fresh exact response must still replace the original successfully.
            gui.click("speedUpCloseButton")
            gui.wait_for_property("speedUpOverlay", "visible", False)
            exact_signer_config(harness, mode="match")
            gui.click("speedUpBannerPrimaryButton")
            gui.wait_for_property("speedUpOverlay", "opened", True)
            gui.wait_for_property("updateTransactionButton", "enabled", True, timeout_ms=30000)
            gui.click("updateTransactionButton")
            wait_until(lambda: expected not in rpc_call(harness.gui_rpc_port, "getrawmempool"),
                       description="exact approved replacement")
            pool = rpc_call(harness.gui_rpc_port, "getrawmempool")
            assert len(pool) == 1 and counterfeit["txid"] not in pool
            replacement_txid = pool[0]
            original = rpc_call(harness.gui_rpc_port, "gettransaction", [expected], wallet=wallet_name)
            assert replacement_txid in original["walletconflicts"]
            decoded = rpc_call(harness.gui_rpc_port, "getrawtransaction", [replacement_txid, True])
            assert original_inputs.intersection({(item["txid"], item["vout"]) for item in decoded["vin"]})
            rpc_call(harness.gui_rpc_port, "generatetoaddress", [1, mining])
            wait_until(lambda: rpc_call(harness.gui_rpc_port, "gettransaction", [replacement_txid],
                                      wallet=wallet_name)["confirmations"] >= 1, description="matching replacement confirmation")
            print("[oracle] exact replacement accepted, original conflict preserved, replacement confirmed")
        else:
            inputs = [{"txid": item["txid"], "vout": item["vout"]} for item in approved["vin"]]
            reply, counterfeit = substituted_reply(harness, other, inputs)
            assert counterfeit["txid"] != expected
            exact_signer_config(harness, mode="reply", psbt=reply)
            harness.driver.click("sendTransactionReviewSignerButton")
            assert_signer_status(harness.driver, "sendTransactionReviewSignerStatus", "External signer changed the reviewed transaction.")
            _, retained = export_review(harness, "retained")
            assert retained["txid"] == approved["txid"]
            assert rpc_call(harness.gui_rpc_port, "getrawmempool") == []
            assert harness.driver.get_text("sendTransactionReviewSignerButton") == "Retry external signer"
            exact_signer_config(harness, mode="match")
            harness.driver.click("sendTransactionReviewSignerButton")
            assert_signer_status(harness.driver, "sendTransactionReviewSignerStatus", "Signed on external signer. Ready to send.")
            harness.driver.click("sendTransactionReviewSignerButton")
            wait_until(lambda: expected in rpc_call(harness.gui_rpc_port, "getrawmempool"), description="matching original payment")
            assert counterfeit["txid"] not in rpc_call(harness.gui_rpc_port, "getrawmempool")
            print("[oracle] independently valid substituted payment rejected; exact matching retry accepted")
        with open(os.path.join(harness.tmpdir, "signer-jobs.jsonl"), encoding="utf8") as jobs:
            effects = [json.loads(line) for line in jobs]
        assert len(effects) == (3 if replacement else 2), effects
        print(f"[oracle] signer subprocess effects={len(effects)}")
        return 0
    except Exception as err:  # noqa: BLE001 - preserve the failed scenario and process evidence
        fail_case(case_name, harness, checkpoints, err)
        return 1
    finally:
        harness.stop()


def substitution_send(args):
    return substitution_journey(args, False)


def substitution_speed_up(args):
    return substitution_journey(args, True)


if __name__ == "__main__":
    raise SystemExit(run_scenarios('External signer subprocess and restart journey', [
        Scenario("signer-devices-and-restart", 8, run_test),
        Scenario("signer-substitution-send", 8, substitution_send),
        Scenario("signer-substitution-speed-up", 8, substitution_speed_up),
    ]))
