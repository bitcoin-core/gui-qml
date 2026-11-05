// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/motionpreference.h>

#include <QtGlobal>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

// macOS is in motionpreference_mac.mm.
#ifndef Q_OS_MACOS
bool SystemPrefersReducedMotion()
{
#ifdef Q_OS_WIN
    // "Show animations in Windows". Off means no animations, but it maps to
    // reduced motion so the app behaves the same on every platform.
    BOOL animations{TRUE};
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0)) {
        return !animations;
    }
#endif
    return false;
}
#endif
