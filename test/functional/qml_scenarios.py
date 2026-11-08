# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Select and report independently runnable GUI process scenarios by feature stage."""

import argparse
import json
import time
import traceback
from contextlib import contextmanager
from dataclasses import dataclass
from fnmatch import fnmatchcase
from typing import Callable


@dataclass(frozen=True)
class Scenario:
    name: str
    stage: int
    action: Callable[[argparse.Namespace], object]


def scenario(name, stage, action, *args, **kwargs):
    return Scenario(name, stage, lambda _options: action(*args, **kwargs))


@contextmanager
def gui_process(**kwargs):
    """Own a startup process and check its exit even when a case restarts it."""
    from qml_process_checks import check_gui_exit, report_gui_failure
    from qml_test_harness import QmlTestHarness
    cleanup = kwargs.pop("cleanup", True)
    harness = QmlTestHarness(**kwargs)
    try:
        harness.start()
        yield harness
    except BaseException:
        report_gui_failure(harness.process, "startup scenario")
        raise
    else:
        process = harness.process
        harness.stop(cleanup=False)
        if process is not None:
            _, stderr = process.communicate()
            check_gui_exit(process.returncode, stderr.decode("utf8", errors="replace"), socket_path=harness.socket_path)
    finally:
        harness.stop(cleanup=cleanup)


def run_scenarios(description, scenarios):
    parser = argparse.ArgumentParser(description=description)
    parser.add_argument("--list", action="store_true", help="List selected scenario names and stages as JSON without starting the GUI")
    parser.add_argument("--case", action="append", default=[], metavar="PATTERN", help="Select a scenario name or wildcard; may be repeated")
    parser.add_argument("--stage", action="append", type=int, choices=range(1, 10), help="Select scenarios introduced in this stage; may be repeated")
    parser.add_argument("--save-screenshots", action="store_true")
    options = parser.parse_args()
    names = [case.name for case in scenarios]
    if len(names) != len(set(names)) or any(case.stage not in range(1, 10) for case in scenarios):
        parser.error("Scenario names must be unique and stages must be between 1 and 9")
    for pattern in options.case:
        if not any(fnmatchcase(name, pattern) for name in names):
            parser.error(f"No scenario matches {pattern!r}")
    selected = [case for case in scenarios
                if (not options.stage or case.stage in options.stage)
                and (not options.case or any(fnmatchcase(case.name, pattern) for pattern in options.case))]
    if not selected:
        parser.error("No scenarios match the selected cases and stages")
    if options.list:
        print(json.dumps([{"name": case.name, "stage": case.stage} for case in selected]))
        return 0

    failed = False
    passed = 0
    for case in selected:
        print(f"[RUN] stage={case.stage} case={case.name}", flush=True)
        started = time.monotonic()
        try:
            status = case.action(options)
            if status not in (None, 0, 77):
                raise AssertionError(f"Scenario returned status {status}")
        except SystemExit as error:
            status = error.code
            if status not in (0, 77):
                failed = True
        except Exception:
            traceback.print_exc()
            status = 1
            failed = True
        if status in (None, 0):
            passed += 1
        result = "SKIP" if status == 77 else "PASS" if status in (None, 0) else "FAIL"
        print(f"[{result}] stage={case.stage} case={case.name} seconds={time.monotonic() - started:.3f}", flush=True)
    return 1 if failed else 0 if passed else 77
