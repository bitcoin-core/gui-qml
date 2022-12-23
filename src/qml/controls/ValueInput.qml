// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15

Control {
    id: root
    required property string parentState
    property string description: ""
    property int descriptionSize: 18
    property color textColor
    state: root.parentState
    signal editingFinished()
    focusPolicy: Qt.StrongFocus

    contentItem: TextEdit {
        font.family: "Inter"
        font.styleName: "Regular"
        font.pixelSize: root.descriptionSize
        color: root.textColor
        text: root.description
        horizontalAlignment: Text.AlignRight
        wrapMode: Text.WordWrap
        onEditingFinished: editingFinished()
    }

    states: [
        State {
            name: "FILLED"
            PropertyChanges { target: root; textColor: Theme.color.neutral9 }
        },
        State {
            name: "HOVER"
            PropertyChanges { target: root; textColor: Theme.color.orangeLight1 }
        },
        State {
            name: "ACTIVE"
            PropertyChanges { target: root; textColor: Theme.color.orange }
        }
    ]

    Behavior on textColor {
        ColorAnimation { duration: 150 }
    }

    background: Rectangle {
        visible: root.visualFocus
        anchors.fill: parent
        anchors.margins: -4
        border.width: 2
        border.color: Theme.color.purple
        radius: 9
        color: "transparent"
    }
}
