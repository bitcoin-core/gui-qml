# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Read runtime defaults from the pinned Core revision without running its jobs."""

import json
from pathlib import Path
import re
import runpy
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def core_sanitizer_env(build):
    wrapper = build / "bitcoin/test/with_sanitizer_env.py"
    if wrapper.is_file():
        return runpy.run_path(str(wrapper))["sanitizer_env"]()
    setup = ROOT / "bitcoin/ci/test/03_test_script.sh"
    names = {"ASAN_OPTIONS", "LSAN_OPTIONS", "UBSAN_OPTIONS"}
    lines = [line for line in setup.read_text(encoding="utf-8").splitlines()
             if re.match(r'^export (ASAN_OPTIONS|LSAN_OPTIONS|UBSAN_OPTIONS)=', line)]
    found = {line.split("=", 1)[0].removeprefix("export ") for line in lines}
    if len(lines) != len(names) or found != names:
        raise RuntimeError("Core runtime exports changed; review sanitizer environment adaptation")
    script = 'export BASE_ROOT_DIR="$1"\n' + "\n".join(lines)
    script += '\npython3 -c \'import json,os; print(json.dumps({k:os.environ[k] for k in ("ASAN_OPTIONS","LSAN_OPTIONS","UBSAN_OPTIONS")}))\''
    return json.loads(subprocess.check_output(["bash", "-c", script, "core-sanitizer-env", str(ROOT / "bitcoin")], text=True))
