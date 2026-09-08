// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Stub for the org.bitcoincore.qt AppMode singleton.
// Used by bitcoinqml_qmltests so Setting.qml can load without the C++ registration.
// isDesktop: true enables hoverEnabled so hover-state tests exercise real behavior.
pragma Singleton
import QtQuick 2.15

QtObject {
    readonly property bool isDesktop: true
    readonly property bool isMobile: !isDesktop
    readonly property string state: isDesktop ? "DESKTOP" : "MOBILE"
    readonly property bool adaptiveSidebarLayoutAvailable: true
    readonly property bool adaptiveSidebarLayout: false
    readonly property bool walletEnabled: true
}
