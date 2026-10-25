// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../controls"

Item {
    id: root

    property var wallet
    property string buttonObjectName: ""
    property string statusObjectName: ""
    property string reviewState: "initial"
    property string errorMessage: ""
    property bool canSend: true

    readonly property string statusText: {
        if (reviewState === "partiallySigned") {
            return root.wallet && root.wallet.currentTransactionReviewMessage.length > 0
                ? root.wallet.currentTransactionReviewMessage
                : qsTr("Signed on external signer. More signatures are required.")
        }
        if (!canSend) return ""
        if (reviewState === "signed") return qsTr("Signed on external signer. Ready to send.")
        if (reviewState === "waiting") return qsTr("Waiting for approval on external signer.")
        if (reviewState === "error") return errorMessage
        return qsTr("Approve on external signer to broadcast this transaction.")
    }
    readonly property color statusColor: {
        if (reviewState === "signed") return Theme.color.green
        if (reviewState === "error") return Theme.color.red
        return Theme.color.neutral7
    }
    readonly property string buttonText: {
        if (reviewState === "signed") return qsTr("Send")
        if (reviewState === "partiallySigned") return qsTr("More signatures required")
        if (reviewState === "waiting") return qsTr("Waiting for approval…")
        if (reviewState === "error") return qsTr("Retry external signer")
        return qsTr("Approve on external signer")
    }

    signal sendRequested()

    Layout.fillWidth: true
    implicitHeight: actionsColumn.height

    function reset() {
        approveTimer.stop()
        reviewState = "initial"
        errorMessage = ""
    }

    function beginApproval() {
        if (reviewState === "waiting") {
            return
        }
        approveTimer.stop()
        reviewState = "waiting"
        errorMessage = ""
        approveTimer.start()
    }

    Component.onDestruction: reset()
    onVisibleChanged: {
        if (!visible) {
            reset()
        }
    }

    Timer {
        id: approveTimer
        interval: 1
        onTriggered: {
            if (root.wallet) {
                root.wallet.approveExternalSignerTransaction()
            }
        }
    }

    Connections {
        target: root.wallet

        function onExternalSignerApprovalSucceeded() {
            root.reviewState = "signed"
            root.errorMessage = ""
        }

        function onExternalSignerApprovalPartiallySucceeded() {
            root.reviewState = "partiallySigned"
            root.errorMessage = ""
        }

        function onExternalSignerApprovalFailed(message, signerNotFound) {
            root.reviewState = "error"
            root.errorMessage = message
        }
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

        ContinueButton {
            objectName: root.buttonObjectName
            Layout.fillWidth: true
            text: root.buttonText
            enabled: root.canSend && root.reviewState !== "waiting" && root.reviewState !== "partiallySigned"
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
