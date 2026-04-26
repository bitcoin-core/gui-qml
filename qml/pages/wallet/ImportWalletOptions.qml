// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Dialogs
import org.bitcoincore.qt 1.0

import "../../controls"
import "../../components"
import "../settings"

Page {
    id: root
    objectName: "importWalletOptions"
    signal back
    signal next
    background: null
    readonly property bool hasImportError: walletController.walletLoadError.length > 0
    readonly property real heroWidth: Math.min(parent.width - 40, 520)
    readonly property int heroTopMargin: 64
    readonly property int importHeroTopMargin: heroTopMargin + 10
    readonly property int heroIconSize: 60
    readonly property int heroTitleTopMargin: 28

    header: NavigationBar2 {
        leftItem: NavButton {
            iconSource: "image://images/caret-left"
            text: qsTr("Back")
            onClicked: root.back()
        }
        rightItem: NavButton {
            visible: root.hasImportError
            text: qsTr("Done")
            onClicked: root.back()
        }
    }

    FileDialog {
        id: fileDialog
        nameFilters: [qsTr("Wallet backup files (*.bak *.dat)"), qsTr("All files (*)")]
        onAccepted: {
            if (fileDialog.selectedFile.toString().length === 0) {
                return
            }
            walletController.importWallet(walletController.normalizeWalletPath(fileDialog.selectedFile.toString()))
        }
    }

    // Kept hidden so functional tests can inject a path until the native file
    // dialog is automatable through the QML test bridge.
    TextField {
        id: automationPathField
        objectName: "importWalletPathField"
        visible: false
    }

    Connections {
        target: walletController
        function onWalletImportSucceeded() {
            root.next()
        }
    }

    ColumnLayout {
        visible: !root.hasImportError
        width: root.heroWidth
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: root.importHeroTopMargin
        spacing: 0

        Item {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: root.heroIconSize
            Layout.preferredHeight: root.heroIconSize

            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: Theme.color.blue
                opacity: 0.2
            }

            Icon {
                anchors.centerIn: parent
                source: "qrc:/icons/file"
                color: Theme.color.blue
                size: 22
                opacity: 1.0
            }
        }

        CoreText {
            objectName: "importWalletTitle"
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: root.heroTitleTopMargin
            Layout.fillWidth: true
            text: qsTr("Import wallet")
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            font.pixelSize: 28
            bold: true
            color: Theme.color.neutral9
        }

        ContinueButton {
            id: chooseFileButton
            objectName: "importWalletChooseFileButton"
            Layout.preferredWidth: Math.min(300, parent.width - 2 * Layout.leftMargin)
            Layout.topMargin: 30
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.alignment: Qt.AlignCenter
            enabled: !walletController.walletLoadInProgress
            text: walletController.walletLoadInProgress ? qsTr("Importing...") : qsTr("Choose a wallet file")
            onClicked: {
                if (automationPathField.text.length > 0) {
                    const automatedPath = walletController.normalizeWalletPath(automationPathField.text)
                    automationPathField.text = ""
                    walletController.importWallet(automatedPath)
                    return
                }
                fileDialog.open()
            }
        }

        CoreText {
            id: statusText
            objectName: "importWalletStatusText"
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.topMargin: 14
            visible: text.length > 0
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: walletController.walletLoadError.length > 0 ? walletController.walletLoadError : walletController.walletLoadWarnings
            color: walletController.walletLoadError.length > 0 ? Theme.color.red : Theme.color.blue
            font.pixelSize: 16
        }
    }

    ColumnLayout {
        id: errorLayout
        objectName: "importWalletErrorView"
        visible: root.hasImportError
        width: root.heroWidth
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: root.heroTopMargin
        spacing: 0

        Item {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: root.heroIconSize
            Layout.preferredHeight: root.heroIconSize

            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: Theme.color.red
            }

            Icon {
                anchors.centerIn: parent
                source: "qrc:/icons/cross"
                color: Theme.color.white
                size: 22
                opacity: 1.0
            }
        }

        CoreText {
            objectName: "importWalletErrorTitle"
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: root.heroTitleTopMargin
            Layout.fillWidth: true
            text: walletController.walletImportErrorTitle
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            font.pixelSize: 28
            bold: true
            color: Theme.color.neutral9
        }

        CoreText {
            objectName: "importWalletErrorDescription"
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 18
            Layout.fillWidth: true
            text: walletController.walletImportErrorDescription
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            font.pixelSize: 18
            color: Theme.color.neutral7
        }

        CoreText {
            objectName: "importWalletErrorHelpText"
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 28
            Layout.fillWidth: true
            visible: text.length > 0
            text: walletController.walletImportErrorHelpText
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            font.pixelSize: 16
            color: Theme.color.neutral6
        }

        ContinueButton {
            id: chooseAnotherFileButton
            objectName: "importWalletChooseAnotherFileButton"
            Layout.preferredWidth: Math.min(340, parent.width)
            Layout.topMargin: 32
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Choose another file")
            onClicked: {
                if (automationPathField.text.length > 0) {
                    const automatedPath = walletController.normalizeWalletPath(automationPathField.text)
                    automationPathField.text = ""
                    walletController.clearWalletLoadStatus()
                    walletController.importWallet(automatedPath)
                    return
                }
                walletController.clearWalletLoadStatus()
                fileDialog.open()
            }
        }
    }
}
