#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Exercise production startup and RPC shutdown, checking exit status and stderr."""

import argparse
import base64
import http.client
import json
import os
from pathlib import Path
import re
import socket
import subprocess
import sys
import tempfile
import time


def unexpected_stderr(stderr, qt_version=""):
    # These messages are emitted by the minimal platform on supported Qt
    # versions. Match complete lines after stripping, including a missing final
    # newline. Sanitizer diagnostics and all other warnings remain failures.
    permitted = [
        r'This plugin does not support application fonts',
        r'Failed to load font resource: ":/fonts/(?:bitcoincoresans/(?:regular|semibold)|robotomono/regular)"',
        r'(?:qt.core.qobject.connect: )?QObject::connect: No such signal QPlatformNativeInterface::systemTrayWindowChanged\(QScreen\*\)',
        r'Running initialization in thread',
    ]
    if re.fullmatch(r'6\.2\.\d+', qt_version):
        # Qt 6.2 warns when a registered singleton already has a context.
        # Qt 6.4's QQmlEnginePrivate::singletonInstance retains it silently.
        permitted.append(r'QQmlEngine::setContextForObject\(\): Object already has a QQmlContext')
    return [line for line in stderr.strip().splitlines()
            if line.strip() and not any(re.fullmatch(pattern + r'(?: \([^()\n]+:\d+\))?', line.strip()) for pattern in permitted)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('record_path', type=Path)
    parser.add_argument('--qt-version', default='')
    options = parser.parse_args()
    binary = options.binary.resolve()
    record_path = options.record_path.resolve()
    with tempfile.TemporaryDirectory(prefix='qt6-process-') as temporary:
        root = Path(temporary)
        runtime = root / 'runtime'
        runtime.mkdir(mode=0o700)
        env = dict(os.environ, QT_QPA_PLATFORM='minimal', QT_QUICK_BACKEND='software',
                   QML_DISABLE_DISK_CACHE='1', XDG_CONFIG_HOME=str(root / 'config'),
                   XDG_RUNTIME_DIR=str(runtime))
        # Hide the summary of existing Core suppressions, not leak diagnostics.
        env['LSAN_OPTIONS'] = env.get('LSAN_OPTIONS', '') + ':print_suppressions=0'
        with socket.socket() as listener:
            listener.bind(('127.0.0.1', 0))
            port = listener.getsockname()[1]
        args = [str(binary), '-regtest', f'-datadir={root}', '-qml_onboarded=1',
                '-disablewallet', '-server=1', f'-rpcport={port}', '-listen=0',
                '-listenonion=0', '-connect=0', '-dnsseed=0', '-fixedseeds=0',
                '-discover=0', '-natpmp=0', '-printtoconsole=0']
        stdout_file = tempfile.TemporaryFile()
        stderr_file = tempfile.TemporaryFile()
        process = subprocess.Popen(args, env=env, stdout=stdout_file, stderr=stderr_file)
        def rpc(method):
            cookie = (root / 'regtest/.cookie').read_text().strip()
            connection = http.client.HTTPConnection('127.0.0.1', port, timeout=2)
            try:
                auth = base64.b64encode(cookie.encode()).decode()
                connection.request('POST', '/', json.dumps({'jsonrpc':'2.0', 'id':1, 'method':method, 'params':[]}),
                                   {'Authorization': f'Basic {auth}', 'Content-Type':'application/json'})
                response = json.loads(connection.getresponse().read())
                if response.get('error'):
                    raise RuntimeError(response['error'])
                return response['result']
            finally:
                connection.close()
        failure = None
        info = None
        try:
            deadline = time.monotonic() + 45
            while time.monotonic() < deadline and process.poll() is None:
                try:
                    info = rpc('getblockchaininfo')
                    break
                except (OSError, ValueError, RuntimeError):
                    time.sleep(0.05)
            assert info is not None and info['chain'] == 'regtest', 'GUI did not initialize regtest RPC'
            rpc('stop')
            process.wait(timeout=30)
        except Exception as error:
            failure = str(error)
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            stdout_file.seek(0)
            stderr_file.seek(0)
            stdout, stderr = stdout_file.read(), stderr_file.read()
            stdout_file.close()
            stderr_file.close()
            result = {'binary':str(binary), 'exit_code':process.returncode,
                      'chain':info['chain'] if info else None, 'failure':failure,
                      'stdout':stdout.decode(errors='replace'), 'stderr':stderr.decode(errors='replace')}
            record_path.write_text(json.dumps(result, indent=2) + '\n')
            print(json.dumps(result, indent=2))
            if failure or process.returncode != 0:
                sys.exit(1)
            if unexpected_stderr(result['stderr'], options.qt_version):
                sys.exit(1)


if __name__ == "__main__":
    main()
