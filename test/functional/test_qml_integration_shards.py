#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Check that discovered case partitions preserve coverage without starting nodes."""

import subprocess
import sys


def main():
    binary = sys.argv[1]

    def discover(*options):
        result = subprocess.run([binary, *options, "-datatags"], check=True,
                                capture_output=True, text=True, timeout=20)
        rows = [line.removeprefix("IntegrationTests run ") for line in result.stdout.splitlines()
                if line.startswith("IntegrationTests run ")]
        assert rows and "selection" not in rows, result.stdout
        assert len(rows) == len(set(rows)), rows
        return rows

    for selection in (("--stage=6",), ("--module=send",),
                      ("--module=send", "--test=maximumAndSweepReviewRespectOwnershipAndSelection")):
        whole = discover(*selection)
        for count in (1, 2, 3):
            parts = [discover(*selection, f"--shard={index}/{count}") for index in range(count)]
            for index, part in enumerate(parts):
                assert part == whole[index::count], (selection, count, index, part)
            combined = [row for part in parts for row in part]
            assert sorted(combined) == sorted(whole)
            assert len(combined) == len(set(combined))

    for value in ("", "0", "0/0", "-1/2", "2/2", "1/-2", "x/2", "0/x", "0/2/3", "0/999999999999"):
        result = subprocess.run([binary, "--stage=6", f"--shard={value}", "-datatags"],
                                capture_output=True, text=True, timeout=20)
        assert result.returncode != 0 and "Specify --shard" in result.stderr, (value, result)

    for options in (("--shard=0/2", "--shard=1/2"),
                    ("--shard=0/2", "--test-profile=unused"),
                    ("--shard=0/2", "--test-discover")):
        result = subprocess.run([binary, "--stage=6", *options, "-datatags"],
                                capture_output=True, text=True, timeout=20)
        assert result.returncode != 0, options

    print("Shard selections are disjoint, exhaustive, and preserve data rows; invalid selections are rejected.")


if __name__ == "__main__":
    main()
