#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Check fatal notifications and failure exits without requiring Qt's QProcess."""

import argparse
import os
import subprocess
import sys
import unittest


class FatalExceptionTests(unittest.TestCase):
    binary = None

    def test_fatal_exceptions_exit_without_draining(self):
        env = dict(os.environ)
        env["BITCOIN_QML_TEST_CLASSES"] = "NodeModelTests"
        env["BITCOIN_QML_FATAL_EXCEPTION_CHILD"] = "1"

        def run(*args):
            return subprocess.run(
                [self.binary, *args, "-platform", "minimal"],
                env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                text=True, encoding="utf-8", errors="replace", timeout=10,
            )

        # Discover the C++ data rows so every fatal case runs on every Qt build.
        listing = run("-datatags")
        self.assertEqual(listing.returncode, 0, listing.stdout)
        function = "fatalExceptionExitsWithoutDraining"
        cases = []
        for line in listing.stdout.splitlines():
            fields = line.split()
            if len(fields) == 3 and fields[:2] == ["NodeModelTests", function]:
                cases.append(fields[2])
        self.assertTrue(cases, listing.stdout)
        for case in cases:
            with self.subTest(case=case):
                child = run(f"{function}:{case}")
                self.assertEqual(child.returncode, 1, child.stdout)
                self.assertIn("fatal notification acknowledged", child.stdout)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", help="Path to bitcoinqml_unit_tests")
    FatalExceptionTests.binary = parser.parse_args().binary
    unittest.main(argv=[sys.argv[0]], verbosity=2)
