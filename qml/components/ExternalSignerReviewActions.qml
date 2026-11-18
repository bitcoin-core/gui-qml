// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

import "../controls"

Item {
    id: root

    property var wallet
    property string buttonObjectName: ""
    property string statusObjectName: ""
    readonly property string reviewState: {
        if (!wallet) return "initial"
        switch (wallet.signerApprovalState) {
        case WalletQmlModel.ApprovalWaiting: return "waiting"
        case WalletQmlModel.ApprovalOtherReview: return "busy"
        case WalletQmlModel.ApprovalSigned: return "signed"
        case WalletQmlModel.ApprovalPartiallySigned: return "partiallySigned"
        case WalletQmlModel.ApprovalFailed: return "error"
        case WalletQmlModel.ApprovalUncertain: return "uncertain"
        case WalletQmlModel.ApprovalUnverified: return "unverified"
        default: return "initial"
        }
    }
    readonly property string errorMessage: wallet ? wallet.signerApprovalError : ""
    property bool canSend: true

    readonly property string statusText: {
        if (reviewState === "partiallySigned") {
            return root.wallet && root.wallet.currentTransactionReviewMessage.length > 0
                ? root.wallet.currentTransactionReviewMessage
                : qsTr("Signed on external signer. More signatures are required.")
        }
        if ((reviewState === "error" || reviewState === "uncertain" || reviewState === "unverified")) return errorMessage
        if (!canSend) return ""
        if (reviewState === "busy") return qsTr("Finishing the previous external signer request.")
        if (reviewState === "signed") return qsTr("Signed on external signer. Ready to send.")
        if (reviewState === "waiting") return qsTr("Waiting for approval on external signer.")
        return qsTr("Approve on external signer to broadcast this transaction.")
    }
    readonly property color statusColor: {
        if (reviewState === "signed") return Theme.color.successText
        if ((reviewState === "error" || reviewState === "uncertain" || reviewState === "unverified")) return Theme.color.dangerText
        return Theme.color.neutral7
    }
    readonly property string buttonText: {
        if (reviewState === "signed") return qsTr("Send")
        if (reviewState === "partiallySigned") return qsTr("More signatures required")
        if (reviewState === "waiting") return qsTr("Waiting for approval…")
        if (reviewState === "busy") return qsTr("External signer busy")
        if (reviewState === "unverified") return qsTr("Unverified signer result")
        if (reviewState === "error" && root.wallet && root.wallet.signerApprovalSignerNotFound) return qsTr("Retry after connecting signer")
        if ((reviewState === "error" || reviewState === "uncertain")) return qsTr("Retry external signer")
        return qsTr("Approve on external signer")
    }

    signal sendRequested()

    Layout.fillWidth: true
    implicitHeight: actionsColumn.height

    function beginApproval() {
        if (wallet && reviewState !== "waiting" && reviewState !== "busy") wallet.approveExternalSignerTransaction()
    }

    ColumnLayout {
        id: actionsColumn
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: Theme.space.xl

        CoreText {
            objectName: root.statusObjectName
            Layout.fillWidth: true
            visible: root.statusText.length > 0
            horizontalAlignment: Text.AlignLeft
            wrap: true
            text: root.statusText
            textStyle: Theme.text.body
            color: root.statusColor
        }

        PrimaryButton {
            objectName: root.buttonObjectName
            Layout.fillWidth: true
            text: root.buttonText
            enabled: root.canSend && root.reviewState !== "waiting" && root.reviewState !== "busy" && root.reviewState !== "partiallySigned" && root.reviewState !== "uncertain" && root.reviewState !== "unverified"
            onClicked: {
                if (root.reviewState === "signed") {
                    root.sendRequested()
                } else {
                    root.beginApproval()
                }
            }
        }
    }
}
