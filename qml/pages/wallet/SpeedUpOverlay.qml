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
    property var bumpModel: null
    property var operationModel: null
    property string operationTxid: ""
    property int bumpState: root.operationModel ? root.operationModel.state : BumpTransactionModel.Idle
    property string bumpErrorText: root.operationModel ? root.operationModel.errorText : ""
    property bool readyToConfirm: root.isCurrentOperation()
        && root.bumpState === BumpTransactionModel.NeedsConfirmation

    function isCurrentOperation() {
        return root.opened && root.operationModel !== null
            && root.operationModel === root.bumpModel && root.operationTxid === root.txid
            && root.operationModel.oldTxid === root.operationTxid
    }

    function requestPassword() {
        speedUpPassphrasePopup.preparing = root.bumpState === BumpTransactionModel.Preparing
        speedUpPassphrasePopup.busy = false
        speedUpPassphrasePopup.errorText = ""
        speedUpPassphrasePopup.open()
    }

    onTxidChanged: if (visible && operationTxid !== txid) close()
    onBumpModelChanged: if (visible && operationModel !== bumpModel) close()

    signal done()
    signal viewNewTransaction(string newTxid)

    title: qsTr("Speed up transaction")
    implicitWidth: 640
    horizontalPadding: width < SizeClass.stackColumnsMax ? Theme.space.xl : Theme.space.page

    onOpened: {
        root.operationModel = root.bumpModel
        root.operationTxid = root.txid
        if (root.bumpModel) {
            root.bumpModel.prepareFeeBump(root.txid, 1)
            if (root.bumpModel.needsUnlock) root.requestPassword()
        }
    }

    onAboutToHide: {
        const model = root.operationModel
        root.operationModel = null
        speedUpPassphrasePopup.close()
        if (model && model.oldTxid === root.operationTxid) model.reset()
    }

    property string newTxid: ""

    signal bumpSucceeded()

    Connections {
        target: root.operationModel
        function onStateChanged() {
            if (root.opened && root.operationModel && root.operationModel.state !== BumpTransactionModel.Idle
                    && root.operationModel.oldTxid !== root.operationTxid) {
                root.close()
                return
            }
            if (root.isCurrentOperation() && root.operationModel.state === BumpTransactionModel.Succeeded) {
                root.newTxid = root.operationModel.newTxid
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
                visible: root.readyToConfirm
                Layout.fillWidth: true
                backgroundColor: Theme.color.surface2
                ValueRow {
                    objectName: "speedUpOriginalFeeRow"
                    amountValue: true
                    animateUnitChanges: false
                    Layout.fillWidth: true
                    title: qsTr("Original fee")
                    value: root.operationModel ? root.operationModel.oldFee : ""
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
                    value: root.operationModel ? root.operationModel.newFee : ""
                    valueTextStyle: Theme.text.amountCaption
                    valueMaximumWidth: root.availableWidth * 0.6
                    showDivider: false
                }
            }

            CoreText {
                objectName: "speedUpErrorText"
                visible: root.bumpState === BumpTransactionModel.Failed
                text: root.bumpErrorText
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
                        if (!root.isCurrentOperation()) {
                            return
                        }
                        const model = root.operationModel
                        if (!model.confirmFeeBump() && root.isCurrentOperation() && model.needsUnlock) root.requestPassword()
                    }
                }
            }
        }
    }

    WalletPassphrasePopup {
        id: speedUpPassphrasePopup
        property bool preparing: false
        parent: Overlay.overlay
        popupObjectName: "speedUpPassphrasePopup"
        passphraseFieldObjectName: "speedUpPassphraseField"
        errorTextObjectName: "speedUpPassphraseErrorText"
        cancelButtonObjectName: "speedUpPassphraseCancelButton"
        confirmButtonObjectName: "speedUpPassphraseConfirmButton"
        titleText: qsTr("Enter wallet password")
        descriptionText: preparing
            //: Password prompt to prepare and sign a replacement transaction for fee review. Nothing is sent yet.
            ? qsTr("Enter your wallet password to prepare this transaction for review.")
            : qsTr("Enter your wallet password to update this transaction.")
        confirmText: preparing
            //: Unlock the wallet to prepare the replacement. Sending requires a later confirmation.
            ? qsTr("Unlock and continue") : qsTr("Unlock and update")
        busyConfirmText: qsTr("Unlocking…")
        onClosed: if (preparing && root.bumpState === BumpTransactionModel.Preparing) root.close()
        onSubmitted: (passphrase) => {
            if (!root.isCurrentOperation()) {
                return
            }
            speedUpPassphrasePopup.busy = true
            const model = root.operationModel
            const succeeded = preparing
                ? model.prepareFeeBumpWithPassphrase(root.operationTxid, 1, passphrase)
                : model.confirmFeeBumpWithPassphrase(passphrase)
            if (succeeded) {
                speedUpPassphrasePopup.busy = false
                speedUpPassphrasePopup.close()
                return
            }
            speedUpPassphrasePopup.busy = false
            if (root.isCurrentOperation()) {
                if (model.needsUnlock) {
                    speedUpPassphrasePopup.errorText = model.errorText
                } else {
                    speedUpPassphrasePopup.close()
                }
            }
        }
    }

}
