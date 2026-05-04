// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../controls"

OptionPopup {
    id: root

    property bool hasLabel: false
    property bool hasMessage: false
    property bool hasNoteSelf: false

    signal addName()
    signal addMessage()
    signal addNoteSelf()
    signal useAsTemplate()
    signal deleteFromHistory()

    implicitWidth: 300
    implicitHeight: column.implicitHeight + 2 * column.anchors.topMargin

    padding: 0
    clip: true
    modal: true
    dim: false

    ColumnLayout {
        id: column
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: 5
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 0

        EllipsisMenuButtonItem {
            Layout.fillWidth: true
            text: qsTr("Add name")
            visible: !root.hasLabel
            onClicked: {
                root.addName()
                root.close()
            }
        }

        EllipsisMenuButtonItem {
            Layout.fillWidth: true
            text: qsTr("Add message")
            visible: !root.hasMessage
            onClicked: {
                root.addMessage()
                root.close()
            }
        }

        EllipsisMenuButtonItem {
            Layout.fillWidth: true
            text: qsTr("Add note to self")
            visible: !root.hasNoteSelf
            onClicked: {
                root.addNoteSelf()
                root.close()
            }
        }

        EllipsisMenuButtonItem {
            Layout.fillWidth: true
            text: qsTr("Save as file")
            enabled: false
            onClicked: root.close()
        }

        EllipsisMenuButtonItem {
            Layout.fillWidth: true
            text: qsTr("Use as template")
            onClicked: {
                root.close()
                root.useAsTemplate()
            }
        }

        EllipsisMenuButtonItem {
            Layout.fillWidth: true
            text: qsTr("Delete from history")
            onClicked: {
                root.close()
                root.deleteFromHistory()
            }
        }
    }
}
