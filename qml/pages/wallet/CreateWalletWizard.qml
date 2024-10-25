// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../controls"
import "../../components"
import "../settings"
import "../wallet"

PageStack {
    id: root

    signal finished()
    property string walletName: ""

    initialItem: Page {
        background: null

        header: NavigationBar2 {
            id: navbar
            rightItem: NavButton {
                text: qsTr("Skip")
                onClicked: {
                    root.finished()
                }
            }
        }

        ColumnLayout {
            id: columnLayout
            width: Math.min(parent.width, 450)
            anchors.horizontalCenter: parent.horizontalCenter

            Image {
                Layout.alignment: Qt.AlignCenter
                source: "image://images/add-wallet-dark"

                sourceSize.width: 200
                sourceSize.height: 200
            }

            Header {
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                header: qsTr("Add a wallet")
                headerBold: true
                description: qsTr("In this early stage of development, only wallet.dat files are supported.")
            }

            ContinueButton {
                Layout.preferredWidth: Math.min(300, parent.width - 2 * Layout.leftMargin)
                Layout.topMargin: 40
                Layout.leftMargin: 20
                Layout.rightMargin: Layout.leftMargin
                Layout.bottomMargin: 20
                Layout.alignment: Qt.AlignCenter
                text: qsTr("Create wallet")
                onClicked: {
                    root.push(intro)
                }
            }

            ContinueButton {
                Layout.preferredWidth: Math.min(300, parent.width - 2 * Layout.leftMargin)
                Layout.leftMargin: 20
                Layout.rightMargin: Layout.leftMargin
                Layout.alignment: Qt.AlignCenter
                text: qsTr("Import wallet")
                borderColor: Theme.color.neutral6
                borderHoverColor: Theme.color.orangeLight1
                borderPressedColor: Theme.color.orangeLight2
                textColor: Theme.color.orange
                backgroundColor: "transparent"
                backgroundHoverColor: "transparent"
                backgroundPressedColor: "transparent"
            }
        }
    }
    Component {
        id: intro
        CreateIntro {
            onBack: root.pop()
            onNext: root.push(name)
        }
    }
    Component {
        id: name
        CreateName {
            id: createName
            onBack: root.pop()
            onNext: {
                root.walletName = createName.walletName
                root.push(password)
            }
        }
    }
    Component {
        id: password
        CreatePassword {
            walletName: root.walletName
            onBack: root.pop()
            onNext: root.push(confirm)
        }
    }
    Component {
        id: confirm
        CreateConfirm {
            onBack: root.pop()
            onNext: root.push(backup)
        }
    }
    Component {
        id: backup
        CreateBackup {
            onBack: root.pop()
            onNext: root.finished()
        }
    }
}
