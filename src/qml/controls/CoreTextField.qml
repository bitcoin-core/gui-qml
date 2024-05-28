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
    font.family: "Inter"
    font.styleName: "Regular"
    font.pixelSize: 18
    color: Theme.color.neutral9
    placeholderTextColor: Theme.color.neutral5
    echoMode: hideText ? TextInput.Password : TextInput.Normal
    rightPadding: hideText ? 46 : 20
    leftPadding: 20
    background: Rectangle {
        border.color: Theme.color.neutral5
        border.width: 1
        color: "transparent"
        radius: 5

        Icon {
            id: visibleIcon
            enabled: hideText
            visible: hideText
            anchors.right: parent.right
            anchors.rightMargin: 12
            size: 24
            anchors.verticalCenter: parent.verticalCenter
            source: "qrc:/icons/visible"
            color: Theme.color.neutral5
            onClicked: {
                if (root.echoMode == TextInput.Password) {
                    root.echoMode = TextInput.Normal
                    visibleIcon.source = "qrc:/icons/hidden"
                } else {
                    root.echoMode = TextInput.Password
                    visibleIcon.source = "qrc:/icons/visible"
                }
            }
        }
    }
}