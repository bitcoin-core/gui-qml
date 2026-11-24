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
    property WalletModel wallet: null
    readonly property FeeBumpOperation operation: bump.operation
    readonly property bool readyToConfirm: bump.readyToConfirm
    readonly property bool readyToPrepareAgain: bump.readyToPrepareAgain
    readonly property bool uncertainOutcome: bump.uncertainOutcome
    property string newTxid: ""
    property int walletRecordState: FeeBumpOperation.WalletRecordUnknown
    property int walletSubmissionState: FeeBumpOperation.WalletSubmissionUnknown

    signal done()
    signal viewNewTransaction(string newTxid)
    signal bumpSucceeded(string newTxid, int walletRecordState, int walletSubmissionState)

    title: qsTr("Speed up transaction")
    implicitWidth: 640
    horizontalPadding: width < SizeClass.stackColumnsMax ? Theme.space.xl : Theme.space.page

    // This overlay's attempt. Hiding it discards a review that was not
    // committed; a committed attempt stays with the wallet, and the next open
    // of this transaction shows its result once.
    FeeBumpController {
        id: bump
        objectName: "speedUpController"
        feeBumps: root.wallet ? root.wallet.feeBumps : null
        active: root.visible
        onChanged: root.presentNativeState()
    }

    onAboutToShow: {
        root.newTxid = ""
        bump.open(root.txid)
    }

    function presentNativeState() {
        if (!root.opened || !bump.operation) return
        if (bump.succeeded) {
            root.newTxid = bump.operation.newTxid
            root.walletRecordState = bump.operation.walletRecordState
            root.walletSubmissionState = bump.operation.walletSubmissionState
            bump.dismiss()
            root.close()
            root.bumpSucceeded(root.newTxid, root.walletRecordState, root.walletSubmissionState)
        } else if (bump.needsUnlock && !bump.operation.busy) {
            speedUpPassphrasePopup.open()
        }
    }
    // Closing from aboutToShow is ignored on Qt 6.4; close a refused open
    // once the popup has opened.
    onOpened: {
        if (!bump.operation) {
            root.close()
            return
        }
        root.presentNativeState()
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
                    value: root.operation ? root.operation.oldFee : ""
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
                    value: root.operation ? root.operation.newFee : ""
                    valueTextStyle: Theme.text.amountCaption
                    valueMaximumWidth: root.availableWidth * 0.6
                    showDivider: false
                }
            }

            CoreText {
                objectName: "speedUpErrorText"
                visible: root.operation !== null && (root.operation.state === FeeBumpOperation.Failed
                    || root.operation.state === FeeBumpOperation.NeedsRevalidation
                    || root.operation.state === FeeBumpOperation.Uncertain)
                text: root.operation ? root.operation.errorText : ""
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
                    text: bump.uncertainOutcome ? qsTr("Check transaction status")
                        : bump.readyToPrepareAgain ? qsTr("Prepare again") : qsTr("Update transaction")
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    enabled: bump.uncertainOutcome ? root.operation.canRecheckOutcome
                        : bump.readyToConfirm || bump.readyToPrepareAgain
                    onClicked: bump.proceed()
                }

                NeutralButton {
                    objectName: "viewObservedReplacementButton"
                    buttonSize: NeutralButton.Large
                    text: qsTr("View transaction")
                    visible: bump.uncertainOutcome
                        && root.operation.walletRecordState === FeeBumpOperation.WalletRecordRecorded
                        && root.operation.newTxid.length > 0
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    onClicked: {
                        const id = root.operation.newTxid
                        root.close()
                        root.viewNewTransaction(id)
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
        property FeeBumpOperation contextOperation: null
        contextValid: contextOperation !== null && root.opened && root.operation === contextOperation
            && (contextOperation.state === FeeBumpOperation.NeedsConfirmation
                || contextOperation.state === FeeBumpOperation.Committing)
        onAboutToShow: contextOperation = root.operation
        onClosed: contextOperation = null
        busy: contextValid && contextOperation.busy
        errorText: contextValid ? contextOperation.unlockErrorText : ""
        onSubmitted: (passphrase) => {
            if (contextValid && !contextOperation.busy) bump.unlock(passphrase)
        }
    }

}
