// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15

TextField {
    id: root

    property bool hideText: false

    implicitHeight: 56
    implicitWidth: 450
    font.family: "BitcoinCoreSans"
    font.styleName: "Regular"
    font.pixelSize: 18
    color: Theme.color.neutral9
    placeholderTextColor: Theme.color.neutral5
    echoMode: hideText ? TextInput.Password : TextInput.Normal
    verticalAlignment: TextInput.AlignVCenter
    rightPadding: hideText ? 46 : 20
    leftPadding: 20
    background: Rectangle {
        border.color: Theme.color.neutral5
        border.width: 1
        color: "transparent"
        radius: 5
    }

    Icon {
        id: visibleIcon
        visible: root.hideText
        enabled: root.hideText
        z: 1
        size: 24
        anchors.right: root.right
        anchors.rightMargin: 12
        anchors.verticalCenter: root.verticalCenter
        source: root.echoMode === TextInput.Normal ? "qrc:/icons/hidden" : "qrc:/icons/visible"
        color: Theme.color.neutral5

        TapHandler {
            onTapped: root.echoMode = (root.echoMode === TextInput.Password)
                ? TextInput.Normal
                : TextInput.Password
        }

        HoverHandler {
            cursorShape: Qt.PointingHandCursor
        }
    }
}
