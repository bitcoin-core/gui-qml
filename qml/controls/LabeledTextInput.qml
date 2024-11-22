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

    signal iconClicked
    signal textEdited

    id: root
    implicitHeight: label.height + input.height

    CoreText {
        id: label
        anchors.left: parent.left
        anchors.top: parent.top
        color: Theme.color.neutral7
        font.pixelSize: 15
    }

    TextField {
        id: input
        anchors.left: parent.left
        anchors.right: iconContainer.left
        anchors.bottom: parent.bottom
        leftPadding: 0
        font.family: "Inter"
        font.styleName: "Regular"
        font.pixelSize: 18
        color: Theme.color.neutral9
        placeholderTextColor: Theme.color.neutral7
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
            color: Theme.color.neutral8
            size: 30
            enabled: source != ""
            onClicked: root.iconClicked()
        }
    }
}
