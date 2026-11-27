// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

import "../../controls"

Page {
    id: root
    objectName: "sendCompletePage"

    property string txid: ""
    property int targetBlocks: 0
    property int submissionState: WalletModel.SubmissionUnknown
    property int recordState: WalletModel.SendRecordUnknown
    readonly property bool accepted: submissionState === WalletModel.SubmissionMempool
        || submissionState === WalletModel.SubmissionAccepted
    readonly property bool confirmed: submissionState === WalletModel.SubmissionConfirmed
    readonly property bool attention: !accepted && !confirmed
    readonly property string titleText: confirmed ? qsTr("Transaction confirmed")
        : accepted ? qsTr("Transaction submitted")
        : submissionState === WalletModel.SubmissionConflicted ? qsTr("Transaction conflicted")
        : submissionState === WalletModel.SubmissionAbandoned ? qsTr("Transaction abandoned")
        : submissionState === WalletModel.SubmissionUnknown && recordState !== WalletModel.SendRecordRecorded
            ? qsTr("Transaction status unknown")
            : qsTr("Transaction saved")
    readonly property string descriptionText: confirmed ? qsTr("Your transaction is confirmed.")
        : submissionState === WalletModel.SubmissionConflicted
            ? qsTr("A conflicting transaction was observed. Check Activity before trying again.")
        : submissionState === WalletModel.SubmissionAbandoned
            ? qsTr("The transaction was marked abandoned. Check Activity before trying again.")
        : submissionState === WalletModel.SubmissionNotInMempool
            ? qsTr("Your transaction was saved to the wallet, but was not in the mempool when checked. Check Activity for its latest status.")
        : submissionState === WalletModel.SubmissionUnknown && recordState === WalletModel.SendRecordAbsentAtObservation
            ? qsTr("The wallet did not report this transaction in its records when checked. Check Activity before retrying.")
        : submissionState === WalletModel.SubmissionUnknown && recordState !== WalletModel.SendRecordRecorded
            ? qsTr("The wallet record could not be confirmed. Check Activity before retrying.")
        : !accepted
            ? qsTr("Your transaction was saved to the wallet. Its submission status could not be confirmed. Check Activity for its latest status.")
        : targetBlocks >= 10
            ? qsTr("Based on the selected fee rate, confirmation may take %1 or more blocks.").arg(targetBlocks)
        : targetBlocks > 0
            ? qsTr("Based on the selected fee rate, it should confirm within %1 blocks.").arg(targetBlocks)
        : qsTr("Your node accepted the transaction. Confirmation depends on its fee rate.")

    Translate { id: messageOffset; y: 12 }
    Translate { id: actionsOffset; y: 12 }

    signal done()
    signal viewNewTransaction(string txid)

    function focusInitialControl() {
        doneButton.forceActiveFocus(Qt.TabFocusReason)
    }

    StackView.onActivated: root.focusInitialControl()

    background: Rectangle { color: Theme.color.background }

    Component.onCompleted: {
        badgeEntrance.start()
        textEntrance.start()
        buttonsEntrance.start()
        // Under reduced motion everything appears at once, without the staged entrance.
        if (Theme.motion.reduced) {
            badgeEntrance.complete()
            textEntrance.complete()
            buttonsEntrance.complete()
        }
    }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.max(0, Math.min(parent.width - 40, 480))
        spacing: 0

        Item {
            id: successBadge
            objectName: "sendCompleteSuccessBadge"
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 88
            Layout.preferredHeight: 88
            opacity: 0
            rotation: 80
            transform: Translate { id: badgeOffset; y: 40 }

            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: root.attention ? Theme.color.warning : Theme.color.success
            }

            CoreText {
                objectName: "sendCompleteStatusGlyph"
                anchors.centerIn: parent
                visible: root.attention
                text: root.submissionState === WalletModel.SubmissionUnknown ? "?" : "!"
                color: Theme.color.accentForeground
                textStyle: Theme.text.headline
            }

            Canvas {
                id: checkCanvas
                anchors.centerIn: parent
                width: 40
                height: 40
                antialiasing: true
                visible: !root.attention
                property real progress: 0
                onProgressChanged: requestPaint()
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    if (progress <= 0) return

                    const points = [[5, 21], [16, 31], [35, 9]]
                    const firstLength = Math.hypot(points[1][0] - points[0][0],
                                                   points[1][1] - points[0][1])
                    const secondLength = Math.hypot(points[2][0] - points[1][0],
                                                    points[2][1] - points[1][1])
                    let remaining = progress * (firstLength + secondLength)

                    ctx.beginPath()
                    ctx.moveTo(points[0][0], points[0][1])
                    for (let i = 0; i < 2 && remaining > 0; ++i) {
                        const start = points[i]
                        const end = points[i + 1]
                        const length = i === 0 ? firstLength : secondLength
                        const fraction = Math.min(1, remaining / length)
                        ctx.lineTo(start[0] + (end[0] - start[0]) * fraction,
                                   start[1] + (end[1] - start[1]) * fraction)
                        remaining -= length
                    }
                    ctx.strokeStyle = Theme.color.accentForeground
                    ctx.lineWidth = 5
                    ctx.lineCap = "round"
                    ctx.lineJoin = "round"
                    ctx.stroke()
                }
            }
        }

        ColumnLayout {
            id: message
            Layout.fillWidth: true
            Layout.topMargin: 28
            spacing: 10
            opacity: 0
            transform: messageOffset

            CoreText {
                Layout.fillWidth: true
                objectName: "sendCompleteTitle"
                text: root.titleText
                textStyle: Theme.text.headline
                horizontalAlignment: Text.AlignHCenter
            }

            CoreText {
                objectName: "sendCompleteDescription"
                Layout.fillWidth: true
                text: root.descriptionText
                color: Theme.color.neutral7
                textStyle: Theme.text.description
                horizontalAlignment: Text.AlignHCenter
            }
        }

        GridLayout {
            id: actions
            Layout.fillWidth: true
            Layout.topMargin: 32
            columns: width < SizeClass.stackColumnsMax ? 1 : 2
            columnSpacing: 12
            rowSpacing: 12
            opacity: 0
            transform: actionsOffset

            NeutralButton {
                objectName: "sendResultViewTransactionButton"
                Layout.fillWidth: true
                Layout.preferredHeight: 46
                buttonSize: NeutralButton.Large
                text: qsTr("View transaction")
                onClicked: root.viewNewTransaction(root.txid)
            }

            PrimaryButton {
                id: doneButton
                objectName: "sendResultDoneButton"
                Layout.fillWidth: true
                Layout.preferredHeight: 46
                text: qsTr("Done")
                onClicked: root.done()
            }
        }
    }

    ParallelAnimation {
        id: badgeEntrance
        SequentialAnimation {
            PauseAnimation { duration: 180 }
            ParallelAnimation {
                NumberAnimation { target: successBadge; property: "opacity"; from: 0; to: 1; duration: 500; easing.type: Easing.OutCubic }
                NumberAnimation { target: successBadge; property: "rotation"; from: 80; to: 0; duration: 500; easing.type: Easing.OutCubic }
                NumberAnimation { target: badgeOffset; property: "y"; from: 40; to: 0; duration: 500; easing.type: Easing.OutBack }
            }
        }
        SequentialAnimation {
            PauseAnimation { duration: 260 }
            NumberAnimation { target: checkCanvas; property: "progress"; from: 0; to: 1; duration: 500; easing.type: Easing.OutCubic }
        }
    }

    SequentialAnimation {
        id: textEntrance
        PauseAnimation { duration: 260 }
        ParallelAnimation {
            NumberAnimation { target: message; property: "opacity"; from: 0; to: 1; duration: 420; easing.type: Easing.OutCubic }
            NumberAnimation { target: messageOffset; property: "y"; from: 12; to: 0; duration: 420; easing.type: Easing.OutCubic }
        }
    }

    SequentialAnimation {
        id: buttonsEntrance
        PauseAnimation { duration: 340 }
        ParallelAnimation {
            NumberAnimation { target: actions; property: "opacity"; from: 0; to: 1; duration: 420; easing.type: Easing.OutCubic }
            NumberAnimation { target: actionsOffset; property: "y"; from: 12; to: 0; duration: 420; easing.type: Easing.OutCubic }
        }
    }
}
