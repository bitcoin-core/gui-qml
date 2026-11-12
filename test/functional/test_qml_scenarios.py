#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Regression checks for scenario-suite exit status and selection."""

import contextlib
import io
import unittest
from unittest.mock import patch

from qml_scenarios import run_scenarios, scenario


class ScenarioStatusTests(unittest.TestCase):
    def run_statuses(self, statuses):
        def action(status):
            if isinstance(status, BaseException):
                raise status
            return status

        cases = [scenario(str(index), 1, action, status) for index, status in enumerate(statuses)]
        with patch("sys.argv", ["suite"]), contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            return run_scenarios("test", cases)

    def test_pass(self):
        self.assertEqual(self.run_statuses([None, 0]), 0)

    def test_any_skip_prevents_success(self):
        for statuses in ([77], [0, 77], [77, 0], [0, SystemExit(77)]):
            with self.subTest(statuses=statuses):
                self.assertEqual(self.run_statuses(statuses), 77)

    def test_failures_take_precedence(self):
        for statuses in ([77, 1], [SystemExit(2), 77], [0, RuntimeError("failure")]):
            with self.subTest(statuses=statuses):
                self.assertEqual(self.run_statuses(statuses), 1)

    def test_stage_selection_is_exact_and_repeatable(self):
        calls = []
        cases = [scenario(str(stage), stage, calls.append, stage) for stage in (1, 2, 3)]
        with patch("sys.argv", ["suite", "--stage=2", "--stage=3"]), contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(run_scenarios("test", cases), 0)
        self.assertEqual(calls, [2, 3])


if __name__ == "__main__":
    unittest.main()
