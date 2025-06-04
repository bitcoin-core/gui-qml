// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    property alias labelText: label.text
    property alias text: input.text
    property alias placeholderText: input.placeholderText
    property alias iconSource: icon.source
    property alias customIcon: iconContainer.data
    property alias enabled: input.enabled
    property alias validator: input.validator

    signal iconClicked
    signal textEdited

    id: root
    implicitHeight: input.height

    CoreText {
        id: label
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        horizontalAlignment: Text.AlignLeft
        width: 110
        font.pixelSize: 18
    }

    TextField {
        id: input
        anchors.left: label.right
        anchors.right: iconContainer.left
        anchors.verticalCenter: parent.verticalCenter
        leftPadding: 0
        font.family: "Inter"
        font.styleName: "Regular"
        font.pixelSize: 18
        color: Theme.color.neutral9
        placeholderTextColor: enabled ? Theme.color.neutral7 : Theme.color.neutral4
        background: Item {}
        selectByMouse: true
        onTextEdited: root.textEdited()
    }

    Item {
        id: iconContainer
        anchors.right: parent.right
        anchors.verticalCenter: input.verticalCenter

        Icon {
            id: icon
            source: ""
            color: enabled ? Theme.color.neutral8 : Theme.color.neutral4
            size: 30
            enabled: source != ""
            onClicked: root.iconClicked()
        }
    }
}
