// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Dialogs 1.2
import org.bitcoincore.qt 1.0

import "../../controls"
import "../../components"

Popup {
    id: root
    modal: true
    anchors.centerIn: parent

    background: Rectangle {
        anchors.centerIn: parent
        width: columnLayout.width + 40
        height: columnLayout.height + 40
        color: Theme.color.neutral0
        border.color: Theme.color.neutral4
        border.width: 1
        radius: 5
    }

    ColumnLayout {
        id: columnLayout
        anchors.centerIn: parent
        spacing: 20

        Item {
            width: 60
            height: 60
            Layout.alignment: Qt.AlignHCenter
            Rectangle {
                anchors.fill: parent
                Layout.alignment: Qt.AlignHCenter
                radius: 30
                color: Theme.color.green
                opacity: 0.2
            }
            Icon {
                anchors.centerIn: parent
                source: "qrc:/icons/check"
                color: Theme.color.green
                size: 30
                opacity: 1.0
            }
        }

        CoreText {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Transaction sent")
            font.pixelSize: 28
            bold: true
        }

        CoreText {
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: 350
            color: Theme.color.neutral7
            text: qsTr("Based on your selected fee, it should be confirmed within the next 10 minutes.")
            font.pixelSize: 18
        }

        ContinueButton {
            Layout.preferredWidth: Math.min(200, parent.width - 2 * Layout.leftMargin)
            Layout.leftMargin: 20
            Layout.rightMargin: Layout.leftMargin
            Layout.alignment: Qt.AlignCenter
            text: qsTr("Close window")
            borderColor: Theme.color.neutral6
            borderHoverColor: Theme.color.neutral9
            borderPressedColor: Theme.color.neutral9
            textColor: Theme.color.neutral9
            backgroundColor: "transparent"
            backgroundHoverColor: "transparent"
            backgroundPressedColor: "transparent"
            onClicked: {
                root.close()
            }
        }
    }
}
