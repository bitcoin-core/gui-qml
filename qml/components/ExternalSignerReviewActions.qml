// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15

import "../controls"

Item {
    id: root

    // Matches WalletModel::ApprovalState without coupling this reusable
    // component to WalletModel.
    enum ApprovalState {
        Initial = 0,
        Waiting = 1,
        Signed = 2,
        PartiallySigned = 3,
        Failed = 4,
        Closed = 5,
        OtherReview = 6,
        Uncertain = 7,
        Unverified = 8
    }

    property int approvalState: ExternalSignerReviewActions.Initial
    property string errorMessage: ""
    property string reviewMessage: ""
    property bool signerNotFound: false
    property string buttonObjectName: ""
    property string statusObjectName: ""
    readonly property string reviewState: {
        switch (root.approvalState) {
        case ExternalSignerReviewActions.Waiting: return "waiting"
        case ExternalSignerReviewActions.OtherReview: return "busy"
        case ExternalSignerReviewActions.Signed: return "signed"
        case ExternalSignerReviewActions.PartiallySigned: return "partiallySigned"
        case ExternalSignerReviewActions.Failed: return "error"
        case ExternalSignerReviewActions.Uncertain: return "uncertain"
        case ExternalSignerReviewActions.Unverified: return "unverified"
        default: return "initial"
        }
    }
    property bool canSend: true

    readonly property string statusText: {
        if (reviewState === "partiallySigned") {
            return root.reviewMessage.length > 0
                ? root.reviewMessage
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
        if (reviewState === "error" && root.signerNotFound) return qsTr("Retry after connecting signer")
        if ((reviewState === "error" || reviewState === "uncertain")) return qsTr("Retry external signer")
        return qsTr("Approve on external signer")
    }

    signal sendRequested()
    signal approvalRequested()

    Layout.fillWidth: true
    implicitHeight: actionsColumn.height

    function beginApproval() {
        if (reviewState !== "waiting" && reviewState !== "busy") root.approvalRequested()
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
