// Copyright (c) 2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

import "../components"
import "../controls"

OptionPopup {
    id: root
    objectName: "sendOptionsPopup"

    property alias coinControlEnabled: coinControlToggle.checked
    property alias multipleRecipientsEnabled: multipleRecipientsToggle.checked

    signal importPsbtFromFileRequested()
    signal clearFormRequested()

    implicitWidth: 305
    implicitHeight: columnLayout.implicitHeight + 10

    clip: true
    modal: true
    dim: false

    ColumnLayout {
        id: columnLayout
        anchors.fill: parent
        anchors.margins: 5
        spacing: 0

        EllipsisMenuToggleItem {
            id: coinControlToggle
            objectName: "sendOptionsCoinControlToggle"
            Layout.fillWidth: true
            Layout.preferredHeight: 33
            Layout.minimumHeight: 33
            Layout.maximumHeight: 33
            bgRadius: 0
            contentTopPadding: 4
            contentBottomPadding: 5
            text: qsTr("Enable Coin control")
        }

        EllipsisMenuToggleItem {
            id: multipleRecipientsToggle
            objectName: "sendOptionsMultipleRecipientsToggle"
            Layout.fillWidth: true
            Layout.preferredHeight: 33
            Layout.minimumHeight: 33
            Layout.maximumHeight: 33
            bgRadius: 0
            contentTopPadding: 4
            contentBottomPadding: 5
            text: qsTr("Multiple Recipients")
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 9

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                height: 1
                color: Theme.color.neutral5
            }
        }

        EllipsisMenuActionItem {
            id: fileImportButton
            objectName: "sendImportPsbtFromFileButton"
            Layout.fillWidth: true
            text: qsTr("Import PSBT from file…")
            leftIconSource: "qrc:/icons/file"
            onClicked: root.importPsbtFromFileRequested()
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 9

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                height: 1
                color: Theme.color.neutral5
            }
        }

        EllipsisMenuActionItem {
            id: clearFormButton
            objectName: "sendClearFormButton"
            Layout.fillWidth: true
            text: qsTr("Clear form")
            leftIconSource: "qrc:/icons/cross"
            onClicked: root.clearFormRequested()
        }
    }
}
