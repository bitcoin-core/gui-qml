// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/motionpreference.h>

#include <QtGlobal>

#ifdef Q_OS_MACOS
#include <AppKit/AppKit.h>

bool SystemPrefersReducedMotion()
{
    return NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion;
}
#endif
