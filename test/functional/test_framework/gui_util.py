#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Helpers for GUI functional tests."""

import re
import unittest


# TestNode strips stderr, so the final warning need not end with a newline.
# Qt builds may include a source location after the missing native tray signal.
MINIMAL_PLATFORM_STDERR = re.compile(
    r"\A(?:(?:This plugin does not support application fonts"
    r"|Failed to load font resource: :/fonts/"
    r"(?:bitcoincoresans/(?:regular|semibold)|robotomono/regular)"
    r"|qt\.core\.qobject\.connect: QObject::connect: No such signal "
    r"QPlatformNativeInterface::systemTrayWindowChanged\(QScreen\*\)"
    r"(?: in [^\r\n]+/qsystemtrayicon_x11\.cpp:\d+)?"
    r")(?:\n|\Z))*\Z"
)


class TestMinimalPlatformStderr(unittest.TestCase):
    def test_known_warnings(self):
        font = "This plugin does not support application fonts"
        tray = (
            "qt.core.qobject.connect: QObject::connect: No such signal "
            "QPlatformNativeInterface::systemTrayWindowChanged(QScreen*)"
        )
        fonts = "\n".join(
            f"{font}\nFailed to load font resource: :/fonts/{name}"
            for name in ("bitcoincoresans/regular", "bitcoincoresans/semibold", "robotomono/regular")
        )
        for stderr in (
            "", font, "\n".join([font] * 3), fonts, tray,
            f"{fonts}\n{tray}",
            f"{fonts}\n{tray} in /usr/qtbase/src/widgets/util/qsystemtrayicon_x11.cpp:166",
        ):
            for ending in ("", "\n") if stderr else ("",):
                with self.subTest(stderr=stderr, ending=ending):
                    self.assertIsNotNone(MINIMAL_PLATFORM_STDERR.search(stderr + ending))

    def test_unexpected_output(self):
        font = "This plugin does not support application fonts"
        for stderr in (
            "QQmlApplicationEngine failed to load component",
            "ERROR: LeakSanitizer: detected memory leaks",
            f"{font}\nERROR: LeakSanitizer: detected memory leaks",
            f"unexpected\n{font}",
            f"{font} unexpected",
            font * 2,
            "Failed to load font resource: :/fonts/unexpected",
            "qt.core.qobject.connect: QObject::connect: No such signal SomeOtherSignal()",
        ):
            with self.subTest(stderr=stderr):
                self.assertIsNone(MINIMAL_PLATFORM_STDERR.search(stderr))
