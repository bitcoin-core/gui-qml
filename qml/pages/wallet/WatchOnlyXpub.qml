// Copyright (c) 2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

import "../../controls"
import "../../components"

Page {
    id: root
    objectName: "watchOnlyXpub"
    signal back
    signal next
    property string xpub: ""
    background: null

    readonly property bool isValidXpub: {
        var t = xpubInput.text.trim()
        if (t.length < 100) return false
        return walletController.validateXpub(t)
    }

    header: NavigationBar2 {
        leftItem: NavButton {
            iconSource: "image://images/caret-left"
            text: qsTr("Back")
            onClicked: root.back()
        }
        centerItem: Item {
            CoreText {
                anchors.centerIn: parent
                text: qsTr("Watch-only wallet")
                font.pixelSize: 18
                bold: true
                color: Theme.color.neutral9
            }
        }
    }

    ColumnLayout {
        id: columnLayout
        width: Math.min(parent.width, 450)
        anchors.horizontalCenter: parent.horizontalCenter

        CoreText {
            Layout.topMargin: 30
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            font.pixelSize: 15
            color: Theme.color.neutral9
            text: qsTr("Enter extended public key (XPUB)")
            horizontalAlignment: Text.AlignLeft
        }

        Item {
            Layout.fillWidth: true
            Layout.topMargin: 5
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            implicitHeight: 56

            CoreTextField {
                id: xpubInput
                objectName: "watchOnlyXpubInput"
                anchors.fill: parent
                focus: true
                placeholderText: qsTr("Enter your key...")
                rightPadding: 46
            }

            Icon {
                objectName: "watchOnlyXpubPasteButton"
                enabled: true
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                source: "image://images/copy"
                color: Theme.color.neutral9
                size: 24
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Paste from clipboard")
                onClicked: {
                    xpubInput.text = Clipboard.text()
                }
            }
        }

        CoreText {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            visible: xpubInput.text.trim().length > 0 && !root.isValidXpub
            text: qsTr("Enter a valid extended public key (xpub)")
            color: Theme.color.orange
            font.pixelSize: 13
            horizontalAlignment: Text.AlignLeft
        }

        ContinueButton {
            objectName: "watchOnlyXpubNextButton"
            Layout.preferredWidth: Math.min(300, parent.width - 2 * Layout.leftMargin)
            Layout.topMargin: 30
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.alignment: Qt.AlignCenter
            text: qsTr("Next")
            enabled: root.isValidXpub
            onClicked: {
                root.xpub = xpubInput.text.trim()
                root.next()
            }
        }
    }
}
