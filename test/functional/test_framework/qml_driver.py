#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""QML test automation bridge client."""

import ctypes
from ctypes import wintypes
import json
import os
import socket
import time


class QmlDriverError(Exception):
    """Raised when the bridge cannot be reached or rejects a command."""


def _raise_if_process_exited(process):
    if process is not None and (returncode := process.poll()) is not None:
        raise QmlDriverError(f"Test bridge process exited with code {returncode}")


class UnixSocketTransport:
    """Connect to the bridge through a Unix domain socket."""

    def __init__(self, socket_path, timeout, process=None):
        self.socket = None
        deadline = time.monotonic() + timeout
        last_error = None
        while time.monotonic() < deadline:
            _raise_if_process_exited(process)
            try:
                self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                self.socket.settimeout(timeout)
                self.socket.connect(socket_path)
                return
            except (ConnectionRefusedError, FileNotFoundError, OSError) as error:
                last_error = error
                self.close()
                time.sleep(0.25)
        raise QmlDriverError(f"Could not connect to test bridge at {socket_path}: {last_error}")

    def close(self):
        if self.socket:
            self.socket.close()
            self.socket = None

    def sendall(self, data):
        self.socket.sendall(data)

    def recv(self, size):
        return self.socket.recv(size)


class WindowsNamedPipeTransport:
    """Connect to the bridge through a Windows named pipe."""

    ERROR_FILE_NOT_FOUND = 2
    ERROR_BROKEN_PIPE = 109
    ERROR_SEM_TIMEOUT = 121
    ERROR_PIPE_BUSY = 231
    ERROR_PIPE_NOT_CONNECTED = 233
    GENERIC_READ = 0x80000000
    GENERIC_WRITE = 0x40000000
    OPEN_EXISTING = 3
    INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value

    def __init__(self, pipe_name, timeout, process=None):
        self.pipe_name = pipe_name
        self.timeout = timeout
        self.process = process
        self.handle = None
        self.kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        self._configure_functions()
        self._connect()

    def _configure_functions(self):
        self.kernel32.WaitNamedPipeW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD]
        self.kernel32.WaitNamedPipeW.restype = wintypes.BOOL
        self.kernel32.CreateFileW.argtypes = [
            wintypes.LPCWSTR,
            wintypes.DWORD,
            wintypes.DWORD,
            wintypes.LPVOID,
            wintypes.DWORD,
            wintypes.DWORD,
            wintypes.HANDLE,
        ]
        self.kernel32.CreateFileW.restype = wintypes.HANDLE
        self.kernel32.PeekNamedPipe.argtypes = [
            wintypes.HANDLE,
            wintypes.LPVOID,
            wintypes.DWORD,
            wintypes.LPDWORD,
            wintypes.LPDWORD,
            wintypes.LPDWORD,
        ]
        self.kernel32.PeekNamedPipe.restype = wintypes.BOOL
        self.kernel32.ReadFile.argtypes = [
            wintypes.HANDLE,
            wintypes.LPVOID,
            wintypes.DWORD,
            wintypes.LPDWORD,
            wintypes.LPVOID,
        ]
        self.kernel32.ReadFile.restype = wintypes.BOOL
        self.kernel32.WriteFile.argtypes = self.kernel32.ReadFile.argtypes
        self.kernel32.WriteFile.restype = wintypes.BOOL
        self.kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
        self.kernel32.CloseHandle.restype = wintypes.BOOL

    def _connect(self):
        deadline = time.monotonic() + self.timeout
        last_error = None
        while time.monotonic() < deadline:
            _raise_if_process_exited(self.process)
            remaining_ms = max(1, min(250, int((deadline - time.monotonic()) * 1000)))
            if self.kernel32.WaitNamedPipeW(self.pipe_name, remaining_ms):
                handle = self.kernel32.CreateFileW(
                    self.pipe_name,
                    self.GENERIC_READ | self.GENERIC_WRITE,
                    0,
                    None,
                    self.OPEN_EXISTING,
                    0,
                    None,
                )
                if handle != self.INVALID_HANDLE_VALUE:
                    self.handle = handle
                    return
                last_error = ctypes.get_last_error()
            else:
                last_error = ctypes.get_last_error()
            if last_error not in (self.ERROR_FILE_NOT_FOUND, self.ERROR_SEM_TIMEOUT, self.ERROR_PIPE_BUSY):
                break
            time.sleep(0.05)
        detail = ctypes.FormatError(last_error).strip() if last_error else "timed out"
        raise QmlDriverError(f"Could not connect to test bridge named pipe: {detail}")

    def close(self):
        if self.handle is not None:
            self.kernel32.CloseHandle(self.handle)
            self.handle = None

    def sendall(self, data):
        offset = 0
        while offset < len(data):
            buffer = ctypes.create_string_buffer(data[offset:])
            written = wintypes.DWORD()
            if not self.kernel32.WriteFile(
                self.handle,
                buffer,
                len(data) - offset,
                ctypes.byref(written),
                None,
            ):
                self._raise_io_error("write")
            if written.value == 0:
                raise QmlDriverError("Test bridge named pipe write returned zero bytes")
            offset += written.value

    def recv(self, size):
        deadline = time.monotonic() + self.timeout
        while time.monotonic() < deadline:
            available = wintypes.DWORD()
            if not self.kernel32.PeekNamedPipe(
                self.handle,
                None,
                0,
                None,
                ctypes.byref(available),
                None,
            ):
                if ctypes.get_last_error() in (self.ERROR_BROKEN_PIPE, self.ERROR_PIPE_NOT_CONNECTED):
                    return b""
                self._raise_io_error("inspect")
            if available.value:
                buffer_size = min(size, available.value)
                buffer = ctypes.create_string_buffer(buffer_size)
                bytes_read = wintypes.DWORD()
                if not self.kernel32.ReadFile(
                    self.handle,
                    buffer,
                    buffer_size,
                    ctypes.byref(bytes_read),
                    None,
                ):
                    if ctypes.get_last_error() in (self.ERROR_BROKEN_PIPE, self.ERROR_PIPE_NOT_CONNECTED):
                        return b""
                    self._raise_io_error("read")
                return buffer.raw[:bytes_read.value]
            time.sleep(0.01)
        raise QmlDriverError("Timed out waiting for test bridge named pipe response")

    def _raise_io_error(self, operation):
        error = ctypes.get_last_error()
        detail = ctypes.FormatError(error).strip()
        raise QmlDriverError(f"Could not {operation} test bridge named pipe: {detail}")


class QmlDriver:
    """Observe and drive the QML application through its local transport."""

    def __init__(self, socket_path, timeout=30, process=None):
        self.socket_path = socket_path
        self.timeout = timeout
        transport = WindowsNamedPipeTransport if os.name == "nt" else UnixSocketTransport
        self.transport = transport(socket_path, timeout, process)
        self.read_buffer = b""

    def close(self):
        if self.transport:
            self.transport.close()
            self.transport = None

    def get_property(self, object_name, property_name):
        response = self._send({"cmd": "get_property", "objectName": object_name, "prop": property_name})
        self._raise_for_error(response, f"get_property({object_name!r}, {property_name!r})")
        return response["value"]

    def list_objects(self):
        response = self._send({"cmd": "list_objects"})
        self._raise_for_error(response, "list_objects")
        return response["objects"]

    def close_window(self):
        response = self._send({"cmd": "close_window"})
        self._raise_for_error(response, "close_window")

    def _send(self, command):
        if not self.transport:
            raise QmlDriverError("Test bridge is not connected")
        self.transport.sendall((json.dumps(command) + "\n").encode("utf8"))
        return self._receive()

    def _receive(self):
        while b"\n" not in self.read_buffer:
            chunk = self.transport.recv(4096)
            if not chunk:
                raise QmlDriverError("Connection closed by test bridge")
            self.read_buffer += chunk
        line, self.read_buffer = self.read_buffer.split(b"\n", 1)
        return json.loads(line)

    @staticmethod
    def _raise_for_error(response, operation):
        if "error" in response:
            raise QmlDriverError(f"{operation} failed: {response['error']}")
