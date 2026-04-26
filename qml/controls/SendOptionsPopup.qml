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
            text: qsTr("Enable Coin control")
        }

        EllipsisMenuToggleItem {
            id: multipleRecipientsToggle
            objectName: "sendOptionsMultipleRecipientsToggle"
            Layout.fillWidth: true
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

        AbstractButton {
            id: fileImportButton
            objectName: "sendImportPsbtFromFileButton"
            Layout.fillWidth: true
            Layout.preferredHeight: 33
            Layout.minimumHeight: 33
            Layout.maximumHeight: 33
            hoverEnabled: AppMode.isDesktop
            padding: 0
            implicitHeight: 33
            text: qsTr("Import PSBT from file…")

            MouseArea {
                anchors.fill: parent
                enabled: false
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
            }

            contentItem: RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 5
                spacing: 5

                RowLayout {
                    Layout.alignment: Qt.AlignVCenter
                    Layout.fillWidth: true
                    spacing: 7

                    Item {
                        Layout.alignment: Qt.AlignVCenter
                        Layout.preferredWidth: 18
                        Layout.preferredHeight: 18

                        Icon {
                            anchors.centerIn: parent
                            source: "qrc:/icons/file"
                            color: fileImportButton.hovered ? Theme.color.neutral9 : Theme.color.neutral7
                            size: 18
                        }
                    }

                    CoreText {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        text: fileImportButton.text
                        horizontalAlignment: Text.AlignLeft
                        font.pixelSize: 15
                        color: fileImportButton.hovered ? Theme.color.neutral9 : Theme.color.neutral7
                    }
                }

                Item {
                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: 18
                    Layout.preferredHeight: 18
                }
            }
            background: Rectangle {
                anchors.fill: parent
                color: fileImportButton.hovered ? Theme.color.neutral2 : "transparent"
                radius: 0

                Behavior on color {
                    ColorAnimation { duration: 150 }
                }
            }
            onClicked: root.importPsbtFromFileRequested()
        }
    }
}
