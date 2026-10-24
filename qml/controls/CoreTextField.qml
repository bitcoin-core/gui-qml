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

    IconButton {
        id: visibilityToggle
        objectName: root.objectName.length > 0 ? root.objectName + "VisibilityToggle" : ""
        visible: root.hideText
        enabled: root.hideText
        z: 1
        size: 32
        iconSize: 24
        anchors.right: root.right
        anchors.rightMargin: 8
        anchors.verticalCenter: root.verticalCenter
        iconSource: root.echoMode === TextInput.Normal ? "qrc:/icons/hidden" : "qrc:/icons/visible"
        iconColor: Theme.color.neutral5
        hoverColor: Theme.color.neutral7
        activeColor: Theme.color.neutral7
        // Clicking keeps focus in the field; Tab still reaches the toggle.
        focusPolicy: Qt.TabFocus
        accessibleName: root.echoMode === TextInput.Normal ? qsTr("Hide password") : qsTr("Show password")
        background: null
        onClicked: root.echoMode = (root.echoMode === TextInput.Password)
            ? TextInput.Normal
            : TextInput.Password
    }
}
