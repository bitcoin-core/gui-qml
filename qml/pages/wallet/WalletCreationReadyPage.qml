// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15
import "../../controls"
import "../../components"

OnboardingView {
    id: root
    objectName: "walletCreationReadyPage"
    showNavigationBar: false
    usesSharedNavigation: true
    property bool watchOnly: false
    property bool modalView: false
    property bool encryptedWallet: false
    property bool navigationBackEnabled: false
    readonly property bool backupRequired: !watchOnly
    signal done()

    title: ""
    heading: watchOnly ? qsTr("Your watch-only wallet is ready") : qsTr("Your wallet is ready")
    subheading: watchOnly
        ? qsTr("You can view its balance and activity here. Spending requires an external signer.")
        : qsTr("Make a small test transaction before storing a larger amount.")
    maximumContentWidth: 800
    showBackButton: false
    showCloseButton: false
    primaryButtonText: ""
    secondaryButtonText: qsTr("Go to wallet")
    secondaryButtonObjectName: "createWalletReadyDoneButton"
    onSecondaryClicked: root.done()

    Component.onCompleted: walletController.clearWalletLocationOpenError()

    imageView: WalletSuccessBadge {
        objectName: "walletCreationSuccessBadge"
    }

    childView: backupRequired ? backupContent : null

    Component {
        id: backupContent
        ColumnLayout {
            spacing: 20

            Separator {
                Layout.fillWidth: true
            }
            CoreText {
                Layout.fillWidth: true
                text: qsTr("BACKUP")
                color: Theme.color.neutral6
                font: Theme.text.captionStrong.font
                horizontalAlignment: Text.AlignLeft
            }

            FormSection {
                objectName: "walletCreationBackupSection"
                Layout.fillWidth: true
                showBackground: false
                title: qsTr("Keep a copy of your wallet file")
                description: root.encryptedWallet
                    ? qsTr("Save it somewhere secure, separate from this computer. You will need both the file and your password to restore access.")
                    : qsTr("Save it somewhere secure, separate from this computer. Anyone with access to an unencrypted copy may be able to spend your bitcoin.")

                NeutralButton {
                    objectName: "createWalletBackupViewFileButton"
                    Layout.topMargin: 16
                    Layout.leftMargin: 4
                    buttonSize: NeutralButton.Large
                    text: qsTr("View file")
                    onClicked: walletController.openSelectedWalletLocation()
                }
                CoreText {
                    objectName: "createWalletBackupErrorText"
                    visible: text.length > 0
                    Layout.fillWidth: true
                    text: walletController.walletLocationOpenError
                    color: Theme.color.red
                    font: Theme.text.caption.font
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignLeft
                }
            }
        }
    }
}
