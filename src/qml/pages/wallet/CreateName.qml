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
    property string walletName: ""
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
        spacing: 30
        anchors.horizontalCenter: parent.horizontalCenter

        Header {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            header: qsTr("Choose a wallet name")
            headerBold: true
        }

        CoreTextField {
            id: walletNameInput
            focus: true
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            placeholderText: qsTr("Eg. My bitcoin wallet...")
            validator: RegExpValidator { regExp: /^[a-zA-Z0-9_]{1,20}$/ }
            onTextChanged: {
                continueButton.enabled = walletNameInput.text.length > 0
            }
        }

        ContinueButton {
            id: continueButton
            Layout.preferredWidth: Math.min(300, parent.width - 2 * Layout.leftMargin)
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.alignment: Qt.AlignCenter
            enabled: walletNameInput.text.length > 0
            text: qsTr("Continue")
            onClicked: {
                console.log("Creating wallet with name: " + walletNameInput.text)
                root.walletName = walletNameInput.text
                root.next()
            }
        }
    }
}