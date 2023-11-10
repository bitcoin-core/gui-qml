// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../controls"
import "../../components"

Page {
    signal settingsClicked
    id: root
    background: null
    clip: true
    header: NavigationBar2 {
        rightItem: NavButton {
            iconSource: "image://images/gear"
            iconHeight: 24
            iconWidth: 24
            onClicked: root.settingsClicked()
        }
    }

    Component.onCompleted: nodeModel.startNodeInitializionThread();

    BlockClock {
        parentWidth: parent.width - 40
        parentHeight: parent.height
        anchors.centerIn: parent
    }
}
