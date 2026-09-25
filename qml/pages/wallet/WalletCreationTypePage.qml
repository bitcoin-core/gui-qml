// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../controls"
import "../../components"

OnboardingView {
    id: root
    objectName: "walletCreationTypePage"
    showNavigationBar: false
    usesSharedNavigation: true
    property bool modalView: false
    property bool importingWallet: false
    signal cancel()
    signal regularSelected()
    signal watchOnlySelected()
    signal externalSignerSelected()
    signal importSelected()

    title: qsTr("Add a wallet")
    heading: qsTr("Choose a wallet type")
    subheading: importingWallet ? qsTr("Importing your wallet file…")
        : qsTr("You can create a new wallet or import from a wallet file.")
    maximumContentWidth: 700
    showCloseButton: modalView && !importingWallet
    showBackButton: !importingWallet && navigationStack && navigationStack.depth > 1
    primaryButtonText: ""
    onCloseClicked: root.cancel()

    childView: ColumnLayout {
        spacing: 10

        WalletCreationTypeRow {
            objectName: "walletTypeRegular"
            Layout.fillWidth: true
            enabled: !root.importingWallet
            title: qsTr("Regular")
            description: qsTr("Fully managed in this application.")
            iconSource: "image://images/key-filled"
            onClicked: root.regularSelected()
        }
        WalletCreationTypeRow {
            objectName: "walletTypeViewOnly"
            Layout.fillWidth: true
            enabled: !root.importingWallet
            title: qsTr("View-only")
            description: qsTr("Keep an eye on another wallet you have.")
            iconSource: "image://images/visible-filled"
            onClicked: root.watchOnlySelected()
        }
        WalletCreationTypeRow {
            objectName: "walletTypeExternalSigner"
            Layout.fillWidth: true
            enabled: !root.importingWallet
            visible: walletController.canCreateExternalSignerWallet
            title: walletController.externalSignerName.length > 0
                ? qsTr("External signer") : qsTr("Hardware wallet")
            description: qsTr("Sign with a connected device")
            iconSource: "image://images/devices-filled"
            onClicked: root.externalSignerSelected()
        }
        Separator {
            objectName: "walletTypeImportSeparator"
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            Layout.topMargin: 8
            Layout.bottomMargin: 8
        }
        WalletCreationTypeRow {
            objectName: "walletTypeImport"
            Layout.fillWidth: true
            enabled: !root.importingWallet
            title: qsTr("Import wallet")
            description: qsTr("Use an existing wallet.dat file")
            iconSource: "image://images/file"
            onClicked: root.importSelected()
        }

        BusyIndicator {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 16
            visible: root.importingWallet
            running: visible
        }

    }
}
