// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../../controls"

Popup {
    id: root
    objectName: "speedUpCompletePopup"

    property string txid: ""
    signal done()
    signal viewNewTransaction(string txid)

    parent: Overlay.overlay
    width: Math.min(560, parent ? parent.width - 40 : 560)
    height: Math.min(implicitHeight, parent ? parent.height - 40 : implicitHeight)
    implicitHeight: content.implicitHeight + topPadding + bottomPadding
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    padding: 24
    modal: true
    dim: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.6) }
    background: Rectangle {
        color: Theme.color.neutral1
        radius: 16
        SurfaceGradientBorder {
            anchors.fill: parent
            surfaceColor: parent.color
            cornerRadius: parent.radius
        }
    }

    contentItem: ColumnLayout {
        id: content
        spacing: 0

        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 70
            Layout.preferredHeight: 70
            radius: width / 2
            color: Theme.color.green

            Icon {
                anchors.centerIn: parent
                source: "qrc:/icons/check.svg"
                color: Theme.color.white
                size: 38
            }
        }

        CoreText {
            Layout.fillWidth: true
            Layout.topMargin: 28
            text: qsTr("Transaction updated")
            font: Theme.text.headline.font
            lineHeight: Theme.text.headline.lineHeight
            lineHeightMode: Text.FixedHeight
            horizontalAlignment: Text.AlignHCenter
        }

        CoreText {
            Layout.fillWidth: true
            Layout.topMargin: 10
            text: qsTr("Your replacement transaction was broadcast. Confirmation depends on its fee rate.")
            color: Theme.color.neutral7
            font: Theme.text.description.font
            lineHeight: Theme.text.description.lineHeight
            lineHeightMode: Text.FixedHeight
            horizontalAlignment: Text.AlignHCenter
            wrap: true
        }

        GridLayout {
            Layout.fillWidth: true
            Layout.topMargin: 32
            columns: width < 340 ? 1 : 2
            columnSpacing: 12
            rowSpacing: 12

            NeutralButton {
                objectName: "speedUpCompleteViewTransactionButton"
                Layout.fillWidth: true
                Layout.preferredHeight: 46
                buttonSize: NeutralButton.Large
                text: qsTr("View new transaction")
                onClicked: root.viewNewTransaction(root.txid)
            }

            ContinueButton {
                objectName: "speedUpCompleteDoneButton"
                Layout.fillWidth: true
                Layout.preferredHeight: 46
                text: qsTr("Done")
                onClicked: root.done()
            }
        }
    }
}
