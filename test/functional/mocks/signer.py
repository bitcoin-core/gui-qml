#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Synthetic signer: preserve Core discovery fixtures, sign the received terms."""

import argparse
import base64
import http.client
import json
from pathlib import Path
import runpy
import sys
from urllib.parse import quote


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", required=True)
    args, remaining = parser.parse_known_args()
    if "--stdin" in remaining:
        remaining.extend(sys.stdin.read().split())
    if "signtx" not in remaining:
        core_mock = Path(__file__).resolve().parents[3] / "bitcoin/test/functional/mocks/signer.py"
        sys.argv = [str(core_mock), *remaining]
        runpy.run_path(str(core_mock), run_name="__main__")
        return
    with open(args.config, encoding="utf8") as source:
        config = json.load(source)
    if config.get("log"):
        with open(config["log"], "a", encoding="utf8") as log:
            log.write(json.dumps({"command": "signtx", "mode": config["mode"]}) + "\n")
    if config["mode"] == "reply":
        response = config["psbt"]
    else:
        psbt = remaining[remaining.index("signtx") + 1]
        conn = http.client.HTTPConnection("127.0.0.1", config["port"], timeout=10)
        auth = base64.b64encode(f"{config['user']}:{config['password']}".encode()).decode("ascii")
        payload = json.dumps({"jsonrpc": "2.0", "id": "synthetic-signer", "method": "walletprocesspsbt",
                              "params": [psbt, True, "ALL", True, False]})
        conn.request("POST", "/wallet/" + quote(config["wallet"], safe=""), payload,
                     {"Content-Type": "application/json", "Authorization": "Basic " + auth})
        result = json.loads(conn.getresponse().read())
        conn.close()
        if result.get("error"):
            print(json.dumps({"error": result["error"]["message"]}))
            return
        response = result["result"]["psbt"]
    print(json.dumps({"psbt": response}))


if __name__ == "__main__":
    main()
