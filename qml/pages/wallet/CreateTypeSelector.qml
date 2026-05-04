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
    objectName: "createTypeSelector"
    signal back
    signal regularSelected
    signal watchOnlySelected
    signal importSelected
    background: null

    header: NavigationBar2 {
        leftItem: NavButton {
            objectName: "typeSelectorBackButton"
            iconSource: "image://images/caret-left"
            text: qsTr("Back")
            onClicked: root.back()
        }
        centerItem: Item {
            CoreText {
                anchors.centerIn: parent
                text: qsTr("Choose a wallet type")
                font.pixelSize: 18
                bold: true
                color: Theme.color.neutral9
            }
        }
    }

    Flickable {
        anchors.fill: parent
        contentHeight: columnLayout.height
        clip: true

        ColumnLayout {
            id: columnLayout
            width: Math.min(parent.width, 450)
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 10

            Item { Layout.preferredHeight: 10 }

            WalletTypeListItem {
                objectName: "walletTypeRegular"
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                title: qsTr("Regular")
                description: qsTr("Fully managed in this application.")
                iconSource: "image://images/singlesig-wallet"
                onClicked: root.regularSelected()
            }

            WalletTypeListItem {
                objectName: "walletTypeMultiKey"
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                title: qsTr("Multi-key")
                description: qsTr("Requires 2 or more wallets or hardware devices to coordinate.")
                iconSource: "image://images/singlesig-wallet"
                enabled: false
            }

            WalletTypeListItem {
                objectName: "walletTypeViewOnly"
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                title: qsTr("Watch-only")
                description: qsTr("Keep an eye on another wallet you have.")
                iconSource: "image://images/visible"
                onClicked: root.watchOnlySelected()
            }

            WalletTypeListItem {
                objectName: "walletTypeImport"
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                title: qsTr("Import wallet")
                description: qsTr("Load an existing wallet.dat file.")
                iconSource: "image://images/file"
                onClicked: root.importSelected()
            }

            WalletTypeListItem {
                objectName: "walletTypeCustom"
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                title: qsTr("Custom")
                description: qsTr("For unique wallet configurations.")
                iconSource: "image://images/gear-outline"
                enabled: false
            }
        }
    }
}
