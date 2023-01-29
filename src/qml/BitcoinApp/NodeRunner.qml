// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BitcoinApp.Controls
import BitcoinApp.Components

Page {
    background: null
    clip: true
    property alias navRightDetail: navbar.rightDetail
    header: NavigationBar {
        id: navbar
    }

    Component.onCompleted: nodeModel.startNodeInitializionThread();

    BlockClock {
        anchors.centerIn: parent
    }
}
