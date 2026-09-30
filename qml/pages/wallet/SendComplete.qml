// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../../controls"

Page {
    id: root
    objectName: "sendCompletePage"

    property string txid: ""
    property int targetBlocks: 0
    readonly property string descriptionText: targetBlocks >= 10
        ? qsTr("Based on the selected fee rate, confirmation may take %1 or more blocks.").arg(targetBlocks)
        : targetBlocks > 0
            ? qsTr("Based on the selected fee rate, it should confirm within %1 blocks.").arg(targetBlocks)
            : qsTr("Your transaction was broadcast. Confirmation depends on its fee rate.")

    signal done()
    signal viewNewTransaction(string txid)

    background: Rectangle { color: Theme.color.background }

    Component.onCompleted: {
        badgeEntrance.start()
        textEntrance.start()
        buttonsEntrance.start()
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
                color: Theme.color.green
            }

            Canvas {
                id: checkCanvas
                anchors.centerIn: parent
                width: 40
                height: 40
                antialiasing: true
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
                    ctx.strokeStyle = Theme.color.white
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
            transform: Translate { id: messageOffset; y: 12 }

            CoreText {
                Layout.fillWidth: true
                text: qsTr("Transaction sent")
                font: Theme.text.headline.font
                lineHeight: Theme.text.headline.lineHeight
                lineHeightMode: Text.FixedHeight
                horizontalAlignment: Text.AlignHCenter
            }

            CoreText {
                objectName: "sendCompleteDescription"
                Layout.fillWidth: true
                text: root.descriptionText
                color: Theme.color.neutral7
                font: Theme.text.description.font
                lineHeight: Theme.text.description.lineHeight
                lineHeightMode: Text.FixedHeight
                horizontalAlignment: Text.AlignHCenter
            }
        }

        GridLayout {
            id: actions
            Layout.fillWidth: true
            Layout.topMargin: 32
            columns: width < 340 ? 1 : 2
            columnSpacing: 12
            rowSpacing: 12
            opacity: 0
            transform: Translate { id: actionsOffset; y: 12 }

            NeutralButton {
                objectName: "sendResultViewTransactionButton"
                Layout.fillWidth: true
                Layout.preferredHeight: 46
                buttonSize: NeutralButton.Large
                text: qsTr("View transaction")
                onClicked: root.viewNewTransaction(root.txid)
            }

            ContinueButton {
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
