// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

import "../../controls"
import "../../components"

DialogPopup {
    id: root
    objectName: "speedUpOverlay"

    property string txid: ""
    property var bumpModel: walletController.selectedWallet
        ? walletController.selectedWallet.bumpModel : null
    property int bumpState: root.bumpModel ? root.bumpModel.state : BumpTransactionModel.Idle
    property string bumpErrorText: root.bumpModel ? root.bumpModel.errorText : ""
    property bool readyToConfirm: root.bumpModel
        && root.bumpModel.state === BumpTransactionModel.NeedsConfirmation

    signal done()
    signal viewNewTransaction(string newTxid)

    title: qsTr("Speed up transaction")
    implicitWidth: 640
    horizontalPadding: width < SizeClass.stackColumnsMax ? Theme.space.xl : Theme.space.page

    onOpened: {
        if (root.bumpModel) {
            root.bumpModel.prepareFeeBump(root.txid, 1)
        }
    }

    onClosed: {
        if (root.bumpModel) {
            root.bumpModel.reset()
        }
    }

    property string newTxid: ""

    signal bumpSucceeded()

    Connections {
        target: root.bumpModel
        function onStateChanged() {
            if (root.bumpModel && root.bumpModel.state === BumpTransactionModel.Succeeded) {
                root.newTxid = root.bumpModel.newTxid
                root.close()
                root.bumpSucceeded()
            }
        }
    }

    contentItem: ScrollView {
        id: contentScroll
        contentWidth: availableWidth
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        ColumnLayout {
            width: contentScroll.availableWidth
            spacing: 0

            DialogHeader {
                Layout.fillWidth: true
                Layout.bottomMargin: Theme.space.xxl
                title: root.title
                description: qsTr("Set a higher transaction fee if you want your transaction to be confirmed faster.")
                closeButtonObjectName: "speedUpCloseButton"
                onCloseRequested: root.close()
            }

            FormSection {
                objectName: "speedUpFeeSection"
                Layout.fillWidth: true
                backgroundColor: Theme.color.surface2
                ValueRow {
                    objectName: "speedUpOriginalFeeRow"
                    amountValue: true
                    animateUnitChanges: false
                    Layout.fillWidth: true
                    title: qsTr("Original fee")
                    value: root.bumpModel ? root.bumpModel.oldFee : ""
                    valueTextStyle: Theme.text.amountCaption
                    valueMaximumWidth: root.availableWidth * 0.6
                    dividerColor: Theme.color.surfaceDivider
                }
                ValueRow {
                    objectName: "speedUpNewFeeRow"
                    amountValue: true
                    animateUnitChanges: false
                    Layout.fillWidth: true
                    title: qsTr("New fee")
                    value: root.bumpModel ? root.bumpModel.newFee : ""
                    valueTextStyle: Theme.text.amountCaption
                    valueMaximumWidth: root.availableWidth * 0.6
                    showDivider: false
                }
            }

            CoreText {
                objectName: "speedUpErrorText"
                visible: root.bumpModel && root.bumpModel.state === BumpTransactionModel.Failed
                text: root.bumpModel ? root.bumpModel.errorText : ""
                textStyle: Theme.text.description
                color: Theme.color.dangerText
                Layout.fillWidth: true
                Layout.topMargin: 10
                wrap: true
            }

            GridLayout {
                Layout.fillWidth: true
                Layout.topMargin: 24
                columns: root.width < SizeClass.stackColumnsMax ? 1 : 2
                columnSpacing: 12
                rowSpacing: 12

                NeutralButton {
                    buttonSize: NeutralButton.Large
                    text: qsTr("Cancel")
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    onClicked: root.close()
                }

                PrimaryButton {
                    objectName: "updateTransactionButton"
                    text: qsTr("Update transaction")
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    enabled: root.readyToConfirm
                    onClicked: {
                        if (!root.bumpModel) {
                            return
                        }
                        if (!root.bumpModel.confirmFeeBump() && root.bumpModel.needsUnlock) {
                            speedUpPassphrasePopup.busy = false
                            speedUpPassphrasePopup.errorText = ""
                            speedUpPassphrasePopup.open()
                        }
                    }
                }
            }
        }
    }

    WalletPassphrasePopup {
        id: speedUpPassphrasePopup
        parent: Overlay.overlay
        popupObjectName: "speedUpPassphrasePopup"
        passphraseFieldObjectName: "speedUpPassphraseField"
        errorTextObjectName: "speedUpPassphraseErrorText"
        cancelButtonObjectName: "speedUpPassphraseCancelButton"
        confirmButtonObjectName: "speedUpPassphraseConfirmButton"
        titleText: qsTr("Enter wallet password")
        descriptionText: qsTr("Enter your wallet password to update this transaction.")
        confirmText: qsTr("Unlock and update")
        busyConfirmText: qsTr("Unlocking…")
        onSubmitted: (passphrase) => {
            if (!root.bumpModel) {
                return
            }
            speedUpPassphrasePopup.busy = true
            if (root.bumpModel.confirmFeeBumpWithPassphrase(passphrase)) {
                speedUpPassphrasePopup.busy = false
                speedUpPassphrasePopup.close()
                return
            }
            speedUpPassphrasePopup.busy = false
            speedUpPassphrasePopup.errorText = root.bumpModel.errorText
        }
    }

}
