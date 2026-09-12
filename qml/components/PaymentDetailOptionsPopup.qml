// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../controls"

ContextMenu {
    id: root

    property bool hasLabel: false
    property bool hasMessage: false
    property bool hasNoteSelf: false

    signal addName()
    signal addMessage()
    signal addNoteSelf()
    signal useAsTemplate()
    signal deleteFromHistory()

    modal: true
    dim: false

    ContextMenuButton {
        //: Menu entry that opens the request editor on its label field.
        text: qsTr("Add label")
        visible: !root.hasLabel
        onTriggered: root.addName()
    }

    ContextMenuButton {
        text: qsTr("Add message")
        visible: !root.hasMessage
        onTriggered: root.addMessage()
    }

    ContextMenuButton {
        text: qsTr("Add note to self")
        visible: !root.hasNoteSelf
        onTriggered: root.addNoteSelf()
    }

    ContextMenuButton {
        text: qsTr("Save as file")
        enabled: false
    }

    ContextMenuDivider {}

    ContextMenuButton {
        text: qsTr("Use as template")
        onTriggered: root.useAsTemplate()
    }

    ContextMenuButton {
        text: qsTr("Delete from history")
        role: ContextMenuButton.Destructive
        onTriggered: root.deleteFromHistory()
    }
}
