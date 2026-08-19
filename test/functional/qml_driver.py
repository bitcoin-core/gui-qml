#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Driver for the QML test automation bridge.

Connects to the TestBridge Unix domain socket exposed by bitcoin-core-app
when launched with --test-automation=<socket_path>.  Provides a Pythonic
interface for functional tests to observe and drive the QML UI.
"""

import json
import os
import socket
import time


class QmlDriverError(Exception):
    """Raised when the test bridge returns an error response."""


class QmlDriver:
    """Drives the QML GUI via the TestBridge Unix domain socket."""

    def __init__(self, socket_path, timeout=30):
        """Connect to the test bridge.

        Args:
            socket_path: Path to the Unix domain socket.
            timeout: Socket timeout in seconds for individual operations.
        """
        self.socket_path = socket_path
        self.timeout = timeout
        self.sock = None
        self._connect()

    def _connect(self):
        """Establish connection to the test bridge, retrying briefly."""
        deadline = time.time() + self.timeout
        last_err = None
        while time.time() < deadline:
            try:
                self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                self.sock.settimeout(self.timeout)
                self.sock.connect(self.socket_path)
                return
            except (ConnectionRefusedError, FileNotFoundError) as e:
                last_err = e
                if self.sock:
                    self.sock.close()
                    self.sock = None
                time.sleep(0.25)
        raise QmlDriverError(
            f"Could not connect to test bridge at {self.socket_path}: {last_err}"
        )

    def close(self):
        """Close the connection."""
        if self.sock:
            self.sock.close()
            self.sock = None

    def reconnect(self, timeout=None):
        """Close the current socket and connect again to the same bridge path."""
        self.close()
        if timeout is not None:
            self.timeout = timeout
        self._connect()

    # ── High-level commands ──────────────────────────────────────────

    def get_current_page(self):
        """Return the objectName (or class name) of the current page."""
        resp = self._send({"cmd": "get_current_page"})
        if "error" in resp:
            raise QmlDriverError(
                f"get_current_page() failed: {resp['error']}"
            )
        return resp["page"]

    def click(self, object_name):
        """Simulate a click on the named QML object."""
        resp = self._send({"cmd": "click", "objectName": object_name})
        if "error" in resp:
            raise QmlDriverError(f"click({object_name!r}) failed: {resp['error']}")

    def set_text(self, object_name, text):
        """Set the text property of the named QML object."""
        resp = self._send({"cmd": "set_text", "objectName": object_name, "text": text})
        if "error" in resp:
            raise QmlDriverError(
                f"set_text({object_name!r}) failed: {resp['error']}"
            )

    def type_text(self, object_name, text):
        """Type text into the named QML object using key events."""
        resp = self._send({"cmd": "type_text", "objectName": object_name, "text": text})
        if "error" in resp:
            raise QmlDriverError(
                f"type_text({object_name!r}) failed: {resp['error']}"
            )

    def get_text(self, object_name):
        """Return the text property of the named QML object."""
        resp = self._send({"cmd": "get_text", "objectName": object_name})
        if "error" in resp:
            raise QmlDriverError(
                f"get_text({object_name!r}) failed: {resp['error']}"
            )
        return resp["text"]

    def click_list_item(self, view_object_name, row_index, delegate_child_object_name=None):
        """Click a delegate row in a view.

        Args:
            view_object_name: objectName of the view itself, for example a ListView.
            row_index: Zero-based delegate index within that view.
            delegate_child_object_name: Optional objectName to find inside that
                specific delegate instance before clicking. If omitted, clicks
                the delegate root item.
        """
        cmd = {"cmd": "click_list_item", "objectName": view_object_name, "index": row_index}
        if delegate_child_object_name is not None:
            cmd["childObjectName"] = delegate_child_object_name
        resp = self._send(cmd)
        if "error" in resp:
            raise QmlDriverError(
                f"click_list_item({view_object_name!r}, {row_index!r}, {delegate_child_object_name!r}) failed: {resp['error']}"
            )

    def get_list_item_property(self, view_object_name, row_index, prop):
        """Return a property value from a delegate row in a view.

        Args:
            view_object_name: objectName of the view itself, for example a ListView.
            row_index: Zero-based delegate index within that view.
            prop: Property name to read from the delegate root item.
        """
        resp = self._send(
            {"cmd": "get_list_item_property", "objectName": view_object_name, "index": row_index, "prop": prop}
        )
        if "error" in resp:
            raise QmlDriverError(
                f"get_list_item_property({view_object_name!r}, {row_index!r}, {prop!r}) failed: {resp['error']}"
            )
        return resp["value"]

    def get_property(self, object_name, prop):
        """Return an arbitrary property value from a named QML object."""
        resp = self._send(
            {"cmd": "get_property", "objectName": object_name, "prop": prop}
        )
        if "error" in resp:
            raise QmlDriverError(
                f"get_property({object_name!r}, {prop!r}) failed: {resp['error']}"
            )
        return resp["value"]

    def set_property(self, object_name, prop, value):
        """Set an arbitrary property on a named QML object."""
        resp = self._send(
            {"cmd": "set_property", "objectName": object_name, "prop": prop, "value": value}
        )
        if "error" in resp:
            raise QmlDriverError(
                f"set_property({object_name!r}, {prop!r}) failed: {resp['error']}"
            )

    def invoke(self, object_name, method, args=None):
        """Invoke a method or signal on a named QML object.

        Prefer user-like helpers such as click() for UI behavior. Invoking a
        QML signal directly can bypass handlers or intermediate control logic.
        """
        resp = self._send(
            {"cmd": "invoke", "objectName": object_name, "method": method, "args": args or []}
        )
        if "error" in resp:
            raise QmlDriverError(
                f"invoke({object_name!r}, {method!r}, {args!r}) failed: {resp['error']}"
            )

    def invoke_property_object(self, object_name, prop, method, args=None):
        """Invoke a method on a QObject-valued property of a named QML object."""
        resp = self._send(
            {
                "cmd": "invoke_property_object",
                "objectName": object_name,
                "prop": prop,
                "method": method,
                "args": args or [],
            }
        )
        if "error" in resp:
            raise QmlDriverError(
                f"invoke_property_object({object_name!r}, {prop!r}, {method!r}) failed: {resp['error']}"
            )
        return resp.get("value")

    def wait_for_property(self, object_name, prop, predicate_or_value, timeout_ms=5000):
        """Poll get_property until the condition is met or timeout expires.

        Args:
            object_name: objectName of the QML object.
            prop: Property name to check.
            predicate_or_value: A callable predicate(value)->bool, or an exact
                value to compare against (equality check).
            timeout_ms: Maximum wait time in milliseconds.

        Returns the value that satisfied the condition.
        Raises QmlDriverError if the timeout expires.
        """
        predicate = (
            predicate_or_value
            if callable(predicate_or_value)
            else lambda v: v == predicate_or_value
        )
        deadline = time.time() + timeout_ms / 1000
        while time.time() < deadline:
            try:
                value = self.get_property(object_name, prop)
            except QmlDriverError:
                time.sleep(0.05)
                continue
            if predicate(value):
                return value
            time.sleep(0.05)
        value = self.get_property(object_name, prop)
        raise QmlDriverError(
            f"wait_for_property({object_name!r}, {prop!r}) timed out; "
            f"last value: {value!r}"
        )

    def wait_for_page(self, page_name, timeout_ms=5000):
        """Block until the named page/object is visible.

        Args:
            page_name: objectName of the page to wait for.
            timeout_ms: Maximum wait time in milliseconds.
        """
        resp = self._send(
            {"cmd": "wait_for_page", "page": page_name, "timeout": timeout_ms}
        )
        if "error" in resp:
            raise QmlDriverError(
                f"wait_for_page({page_name!r}) failed: {resp['error']}"
            )



    def list_objects(self):
        """Return a list of dicts with objectName and className for all
        named objects in the QML tree.  Useful for debugging."""
        resp = self._send({"cmd": "list_objects"})
        if "error" in resp:
            raise QmlDriverError(f"list_objects failed: {resp['error']}")
        return resp["objects"]

    def get_context_property(self, name):
        """Return metadata for a QQmlEngine root context property."""
        resp = self._send({"cmd": "get_context_property", "name": name})
        if "error" in resp:
            raise QmlDriverError(
                f"get_context_property({name!r}) failed: {resp['error']}"
            )
        return resp

    def save_screenshot(self, path):
        """Save a screenshot of the current QML window to a PNG file.

        Screenshots are intended to capture stable checkpoint states, so wait
        for the relevant StackView transitions to finish before asking the test
        bridge to render the window contents.
        """
        directory = os.path.dirname(path)
        if directory:
            os.makedirs(directory, exist_ok=True)
        self.settle()
        resp = self._send({"cmd": "save_screenshot", "path": path})
        if "error" in resp:
            raise QmlDriverError(
                f"save_screenshot({path!r}) failed: {resp['error']}"
            )
        return resp

    def show_runtime_dialog(self, message, style, question=False):
        """Open a NodeRuntimeDialog through the test automation bridge."""
        resp = self._send(
            {
                "cmd": "show_runtime_dialog",
                "message": message,
                "style": style,
                "question": question,
            }
        )
        if "error" in resp:
            raise QmlDriverError(f"show_runtime_dialog failed: {resp['error']}")

    def answer_runtime_dialog(self, button):
        """Answer the active NodeRuntimeDialog with a Core/QMessageBox button id."""
        resp = self._send({"cmd": "answer_runtime_dialog", "button": button})
        if "error" in resp:
            raise QmlDriverError(f"answer_runtime_dialog({button!r}) failed: {resp['error']}")

    def close_window(self):
        """Send a QCloseEvent to the main application window."""
        resp = self._send({"cmd": "close_window"})
        if "error" in resp:
            raise QmlDriverError(f"close_window failed: {resp['error']}")

    def set_clipboard_text(self, text):
        """Set the system clipboard to the given text string."""
        resp = self._send({"cmd": "set_clipboard_text", "text": text})
        if "error" in resp:
            raise QmlDriverError(f"set_clipboard_text failed: {resp['error']}")

    def settle(
        self,
        timeout_ms=5000,
        stack_view_names=("mainPageStack", "createWalletWizard", "settingsNavigationStack_wallet"),
    ):
        """Wait for relevant StackView transitions to finish.

        The wallet flow transitions run through the app's main page stack and,
        once opened, the nested create-wallet wizard and wallet-settings stacks.
        Waiting for their `busy` property to become false is more reliable than
        sleeping. Missing stack views are ignored so this remains safe before
        nested flows have been created.
        """
        deadline = time.time() + (timeout_ms / 1000)
        last_busy = {}
        while time.time() < deadline:
            any_busy = False
            for object_name in stack_view_names:
                try:
                    busy = self.get_property(object_name, "busy")
                except QmlDriverError as err:
                    msg = str(err)
                    if f"Object not found: {object_name}" in msg:
                        continue
                    if "Property not found:" in msg:
                        continue
                    raise
                last_busy[object_name] = busy
                if busy:
                    any_busy = True
            if not any_busy:
                return
            time.sleep(0.05)
        raise QmlDriverError(
            f"Timed out waiting for stack views to become idle: {last_busy}"
        )

    def wait_for_object(self, object_name, timeout_ms=5000):
        """Block until an object with the given name exists."""
        deadline = time.time() + (timeout_ms / 1000)
        while time.time() < deadline:
            if any(obj.get("objectName") == object_name for obj in self.list_objects()):
                return
            time.sleep(0.1)
        raise QmlDriverError(
            f"wait_for_object({object_name!r}) failed: Object not found before timeout"
        )

    def object_exists(self, object_name):
        """Return whether an object with the given name currently exists."""
        return any(obj.get("objectName") == object_name for obj in self.list_objects())

    # ── Transport layer ──────────────────────────────────────────────

    def _send(self, cmd):
        """Send a JSON command and return the parsed JSON response."""
        payload = json.dumps(cmd) + "\n"
        self.sock.sendall(payload.encode("utf-8"))
        return self._recv()

    def _recv(self):
        """Read a newline-delimited JSON response."""
        buf = b""
        while True:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise QmlDriverError("Connection closed by test bridge")
            buf += chunk
            if b"\n" in buf:
                line, _ = buf.split(b"\n", 1)
                return json.loads(line)
