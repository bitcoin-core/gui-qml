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
    property int walletRecordState: FeeBumpOperation.WalletRecordUnknown
    property int walletSubmissionState: FeeBumpOperation.WalletSubmissionUnknown
    signal done()
    signal viewNewTransaction(string txid)

    tone: AlertPopup.Success
    title: qsTr("Replacement transaction update result")
    // Report only what the wallet observed. It cannot see other nodes, so
    // a replacement in the mempool is not called broadcast.
    message: root.walletSubmissionState === FeeBumpOperation.WalletSubmissionMempool
        ? qsTr("The replacement is in the mempool. Other nodes may not have received it yet.")
        : root.walletSubmissionState === FeeBumpOperation.WalletSubmissionConfirmed
            ? qsTr("The replacement is confirmed.")
        : root.walletSubmissionState === FeeBumpOperation.WalletSubmissionNotInMempool
            ? qsTr("The replacement is saved in the wallet but is not in the node's mempool, so it may not have reached other nodes.")
        : root.walletSubmissionState === FeeBumpOperation.WalletSubmissionConflicted
            ? qsTr("The replacement conflicts with another transaction.")
        : root.walletSubmissionState === FeeBumpOperation.WalletSubmissionAbandoned
            ? qsTr("The replacement was abandoned.")
        : root.walletRecordState === FeeBumpOperation.WalletRecordRecorded
            ? qsTr("The replacement is saved in the wallet. It is not known yet whether it is in the mempool or has reached other nodes. Check Activity for updates.")
        : root.walletRecordState === FeeBumpOperation.WalletRecordAbsentAtObservation
            ? qsTr("The wallet does not show the replacement, so it may not have been sent. Check Activity before trying again.")
            : qsTr("The replacement may not have been saved or sent. Check Activity before trying again.")
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
