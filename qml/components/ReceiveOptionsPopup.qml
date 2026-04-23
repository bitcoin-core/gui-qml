// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../controls"

OptionPopup {
    id: root

    implicitWidth: 300
    implicitHeight: columnLayout.implicitHeight + 20

    clip: true
    modal: true
    dim: false

    ColumnLayout {
        id: columnLayout
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: 5
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 0

        EllipsisMenuButtonItem {
            Layout.fillWidth: true
            text: qsTr("Show address type")
            enabled: false
        }

        EllipsisMenuButtonItem {
            Layout.fillWidth: true
            text: qsTr("View address history")
            enabled: false
        }
    }
}
