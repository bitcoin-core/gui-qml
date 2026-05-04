// Copyright (c) 2024-2026 The Bitcoin Core developers
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
    objectName: "createWalletPasswordPage"
    signal back
    signal next
    background: null

    required property string walletName

    Component.onCompleted: walletController.clearWalletCreateStatus()

    header: NavigationBar2 {
        navigationStack: root.StackView.view
        rightItem: NavButton {
            objectName: "createWalletPasswordSkipButton"
            text: qsTr("Skip")
            enabled: walletController.initialized
            onClicked: {
                if (walletController.createSingleSigWallet(walletName, "")) {
                    root.next()
                }
            }
        }
    }

    ColumnLayout {
        id: columnLayout
        width: Math.min(parent.width, 450)
        anchors.horizontalCenter: parent.horizontalCenter

        Header {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            header: qsTr("Choose a password")
            headerBold: true
            description: qsTr("It is recommended to set a password to protect your wallet file from unwanted access from others.")
        }

        CoreText {
            Layout.topMargin: 30
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            font.pixelSize: 15
            color: Theme.color.neutral9
            text: qsTr("Choose a password")
        }

        CoreTextField {
            id: password
            objectName: "createWalletPasswordInput"
            Layout.fillWidth: true
            Layout.topMargin: 5
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            focus: true
            hideText: true
            placeholderText: qsTr("Enter password...")
            onTextChanged: walletController.clearWalletCreateStatus()
        }
        CoreText {
            Layout.topMargin: 20
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            font.pixelSize: 15
            color: Theme.color.neutral9
            text: qsTr("Confirm password")
        }
        CoreTextField {
            id: passwordRepeat
            objectName: "createWalletPasswordRepeatInput"
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            hideText: true
            placeholderText: qsTr("Enter password again...")
            onTextChanged: walletController.clearWalletCreateStatus()
        }

        Setting {
            id: confirmToggle
            objectName: "createWalletPasswordConfirmToggle"
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            description: qsTr("I understand that if I lose or forget this password I might lose access to the bitcoin stored in this wallet.")
            actionItem: OptionSwitch {
            }
            onClicked: {
                loadedItem.toggle()
                loadedItem.toggled()
            }
        }

        ContinueButton {
            objectName: "createWalletPasswordContinueButton"
            Layout.preferredWidth: Math.min(300, parent.width - 2 * Layout.leftMargin)
            Layout.topMargin: 40
            Layout.leftMargin: 20
            Layout.rightMargin: Layout.leftMargin
            Layout.alignment: Qt.AlignCenter
            text: qsTr("Continue")
            enabled: walletController.initialized &&
                password.text != "" &&
                passwordRepeat.text != "" &&
                password.text == passwordRepeat.text &&
                confirmToggle.loadedItem.checked
            onClicked: {
                if (walletController.createSingleSigWallet(walletName, password.text)) {
                    root.next()
                }
            }
        }

        CoreText {
            objectName: "createWalletPasswordErrorText"
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            visible: text.length > 0
            text: walletController.walletCreateError
            color: Theme.color.red
            font.pixelSize: 15
            wrapMode: Text.WordWrap
        }
    }
}
