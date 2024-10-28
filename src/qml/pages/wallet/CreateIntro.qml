// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../../controls"
import "../../components"
import "../settings"

Page {
    id: root
    signal back
    signal next
    background: null

    header: NavigationBar2 {
        id: navbar
        leftItem: NavButton {
            iconSource: "image://images/caret-left"
            text: qsTr("Back")
            onClicked: {
                root.back()
            }
        }
    }

    ColumnLayout {
        id: columnLayout
        width: Math.min(parent.width, 450)
        anchors.horizontalCenter: parent.horizontalCenter

        Item {
            Layout.alignment: Qt.AlignCenter
            Layout.preferredHeight: circle.height
            Layout.preferredWidth: circle.width
            Rectangle {
                id: circle
                width: 60
                height: width
                radius: width / 2
                color: Theme.color.blue
            }
            Icon {
                source: "image://images/wallet"
                color: Theme.color.white
                width: 30
                height: width
                anchors.centerIn: circle
            }
        }

        CoreText {
            Layout.topMargin: 25
            Layout.alignment: Qt.AlignCenter
            text: qsTr("You are about to create \na single-key bitcoin wallet")
            font.pixelSize: 21
            bold: true
            color: Theme.color.neutral9
        }

        CoreText {
            Layout.topMargin: 20
            Layout.alignment: Qt.AlignCenter
            text: qsTr("You can fully control it in this application.")
            font.pixelSize: 18
            color: Theme.color.neutral7
        }

        Separator {
            Layout.topMargin: 10
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.fillWidth: true
            color: Theme.color.neutral4
        }

        CoreText {
            Layout.topMargin: 10
            Layout.alignment: Qt.AlignCenter
            text: qsTr("Wallet data will be stored locally on your hard drive.")
            font.pixelSize: 18
            color: Theme.color.neutral7
        }

        Separator {
            Layout.topMargin: 10
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.fillWidth: true
            color: Theme.color.neutral4
        }

        CoreText {
            Layout.topMargin: 10
            Layout.alignment: Qt.AlignCenter
            text: qsTr("You can optionally protect it with a password.")
            font.pixelSize: 18
            color: Theme.color.neutral7
        }

        ContinueButton {
            Layout.preferredWidth: Math.min(300, parent.width - 2 * Layout.leftMargin)
            Layout.topMargin: 30
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.alignment: Qt.AlignCenter
            text: qsTr("Start")
            onClicked: {
                root.next()
            }
        }
    }
}