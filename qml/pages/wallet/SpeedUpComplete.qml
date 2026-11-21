// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import org.bitcoincore.qt 1.0

import "../../controls"

AlertPopup {
    id: root
    objectName: "speedUpCompletePopup"

    property string txid: ""
    property int walletRecordState: BumpTransactionModel.WalletRecordUnknown
    property int walletSubmissionState: BumpTransactionModel.WalletSubmissionUnknown
    signal done()
    signal viewNewTransaction(string txid)

    tone: AlertPopup.Success
    title: qsTr("Replacement transaction update result")
    // Report only what the wallet observed; it does not report peer relay.
    message: root.walletSubmissionState === BumpTransactionModel.WalletSubmissionMempool
        ? qsTr("The replacement transaction is in the wallet's mempool. The wallet does not report peer relay.")
        : root.walletSubmissionState === BumpTransactionModel.WalletSubmissionConfirmed
            ? qsTr("The replacement transaction is confirmed. The wallet does not report peer relay.")
        : root.walletSubmissionState === BumpTransactionModel.WalletSubmissionNotInMempool
            ? qsTr("The replacement is recorded by the wallet but is not currently in its mempool. Peer relay is unknown.")
        : root.walletSubmissionState === BumpTransactionModel.WalletSubmissionConflicted
            ? qsTr("The wallet reports that the replacement transaction is conflicted.")
        : root.walletSubmissionState === BumpTransactionModel.WalletSubmissionAbandoned
            ? qsTr("The wallet reports that the replacement transaction is abandoned.")
        : root.walletRecordState === BumpTransactionModel.WalletRecordRecorded
            ? qsTr("The replacement transaction is saved to the wallet. Its mempool status could not be confirmed, and peer relay is unknown. Check Activity for updates.")
        : root.walletRecordState === BumpTransactionModel.WalletRecordAbsentAtObservation
            ? qsTr("The wallet did not report a matching replacement transaction when checked. Submission and peer relay are unknown. Check Activity before retrying.")
            : qsTr("The wallet returned from the fee-bump operation, but its transaction record and submission status could not be confirmed. Peer relay is unknown. Check Activity before retrying.")
    messageObjectName: "speedUpOutcomeText"
    implicitWidth: Math.max(420, horizontalActionsWidth + 40)
    escapeAction: doneAction

    AlertAction {
        text: qsTr("View new transaction")
        role: AlertAction.Neutral
        buttonObjectName: "speedUpCompleteViewTransactionButton"
        closesPopup: false
        onTriggered: root.viewNewTransaction(root.txid)
    }

    AlertAction {
        id: doneAction
        text: qsTr("Done")
        buttonObjectName: "speedUpCompleteDoneButton"
        closesPopup: false
        onTriggered: root.done()
    }
}
