// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../controls"
import "../../components"

Page {
    background: null
    clip: true
    property alias navRightDetail: navbar.rightDetail
    header: NavigationBar {
        id: navbar
    }

    Component.onCompleted: nodeModel.startNodeInitializionThread();

    ColumnLayout {
        spacing: 30
        anchors.centerIn: parent
        BlockClock {
            Layout.alignment: Qt.AlignCenter
        }
        NetworkIndicator {
            Layout.alignment: Qt.AlignCenter
        }
    }
}
