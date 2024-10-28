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

        Header {
            Layout.topMargin: 20
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            header: qsTr("Back up your wallet")
            headerBold: true
            description: qsTr("Your wallet is a file stored on your hard disk.\nTo prevent accidental loss, it is recommended you keep a copy of your wallet file in a secure place, like a dedicated USB Drive.")
        }

        ContinueButton {
            Layout.preferredWidth: Math.min(300, parent.width - 2 * Layout.leftMargin)
            Layout.topMargin: 30
            Layout.leftMargin: 20
            Layout.rightMargin: Layout.leftMargin
            Layout.alignment: Qt.AlignCenter
            text: qsTr("View file")
            borderColor: Theme.color.neutral6
            borderHoverColor: Theme.color.orangeLight1
            borderPressedColor: Theme.color.orangeLight2
            textColor: Theme.color.orange
            backgroundColor: "transparent"
            backgroundHoverColor: "transparent"
            backgroundPressedColor: "transparent"
        }

        ContinueButton {
            Layout.preferredWidth: Math.min(300, parent.width - 2 * Layout.leftMargin)
            Layout.topMargin: 30
            Layout.leftMargin: 20
            Layout.rightMargin: Layout.leftMargin
            Layout.alignment: Qt.AlignCenter
            text: qsTr("Done")
            onClicked: {
                root.next()
            }
        }
    }
}