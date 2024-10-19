// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../controls"
import "../../components"

Page {
    signal back

    id: root
    background: null
    implicitWidth: 450
    leftPadding: 20
    rightPadding: 20
    topPadding: 30

    header: NavigationBar2 {
        leftItem: NavButton {
            iconSource: "image://images/caret-left"
            text: qsTr("Back")
            onClicked: root.back()
        }
        centerItem: Header {
            headerBold: true
            headerSize: 18
            header: qsTr("Theme")
        }
    }
    ThemeSettings {
        width: Math.min(parent.width, 450)
        anchors.horizontalCenter: parent.horizontalCenter
    }
}