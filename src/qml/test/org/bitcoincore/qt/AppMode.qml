// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

pragma Singleton
import QtQuick 2.15

Item {
    property bool isDesktop: true
    property bool isMobile: false
    enum Mode {
        DESKTOP,
        MOBILE
    }
    property string state: "MOBILE"
}
