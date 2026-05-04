// Copyright (c) 2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../../controls"
import "../../components"

Page {
    id: root
    objectName: "watchOnlyIntro"
    signal back
    signal next
    background: null

    header: NavigationBar2 {
        leftItem: NavButton {
            iconSource: "image://images/caret-left"
            text: qsTr("Back")
            onClicked: root.back()
        }
        centerItem: Item {
            CoreText {
                anchors.centerIn: parent
                text: qsTr("Create wallet")
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
                source: "image://images/info"
                color: Theme.color.white
                width: 30
                height: width
                anchors.centerIn: circle
            }
        }

        CoreText {
            Layout.topMargin: 25
            Layout.fillWidth: true
            text: qsTr("You are about to add a watch-only bitcoin wallet")
            font.pixelSize: 21
            bold: true
            color: Theme.color.neutral9
        }

        CoreText {
            Layout.topMargin: 20
            Layout.fillWidth: true
            text: qsTr("You can view transactions and the balance.")
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
            Layout.fillWidth: true
            text: qsTr("Transactions can be initiated, but require an external signer to complete.")
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
            Layout.fillWidth: true
            text: qsTr("Setup requires an extended public key (xpub).")
            font.pixelSize: 18
            color: Theme.color.neutral7
        }

        ContinueButton {
            objectName: "watchOnlyIntroNextButton"
            Layout.preferredWidth: Math.min(300, parent.width - 2 * Layout.leftMargin)
            Layout.topMargin: 30
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.alignment: Qt.AlignCenter
            text: qsTr("Next")
            onClicked: root.next()
        }
    }
}
