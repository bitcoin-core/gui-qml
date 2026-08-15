// Copyright (c) 2021-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls

ApplicationWindow {
    id: root
    objectName: "mainWindow"
    readonly property string nodeStatus: nodeModel ? nodeModel.statusText : qsTr("Not connected")
    title: qsTr("Bitcoin Core")
    minimumWidth: 750
    minimumHeight: 450
    visible: true

    Label {
        anchors.centerIn: parent
        text: root.nodeStatus
    }
}
