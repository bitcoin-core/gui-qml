// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../controls"

Popup {
    id: root
    objectName: "nodeWarningsPopup"

    readonly property int warningCount: warningRepeater.count
    readonly property string firstWarningText: runtimeDialogModel.warningList.length > 0 ? runtimeDialogModel.warningList[0] : ""
    readonly property int firstWarningLineCount: warningRepeater.count > 0 ? warningRepeater.itemAt(0).warningLineCount : 0
    readonly property int firstWarningWrapMode: warningRepeater.count > 0 ? warningRepeater.itemAt(0).warningWrapMode : Text.NoWrap
    readonly property int contentMargin: 28

    modal: true
    padding: 0
    anchors.centerIn: parent
    width: parent ? Math.min(parent.width - (2 * contentMargin), 720) : 720
    height: Math.min(implicitHeight, parent ? parent.height - 80 : 520)
    implicitHeight: Math.min(columnLayout.implicitHeight, 520)

    background: Rectangle {
        color: Theme.color.background
        radius: 8
        border.color: Theme.color.neutral4
        border.width: 1
    }

    ColumnLayout {
        id: columnLayout
        anchors.fill: parent
        spacing: 0

        CoreText {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            text: qsTr("Node warnings")
            bold: true
            font.pixelSize: 20
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        Separator { Layout.fillWidth: true }

        ScrollView {
            id: warningsScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: root.contentMargin
            contentWidth: availableWidth
            clip: true

            ColumnLayout {
                width: warningsScroll.availableWidth
                spacing: 12

                Repeater {
                    id: warningRepeater
                    model: runtimeDialogModel.warningList
                    delegate: RowLayout {
                        // Qt 6.2 does not inject `modelData`/`index` into this
                        // delegate; declare them explicitly.
                        required property var modelData
                        required property int index

                        readonly property int warningLineCount: warningText.lineCount
                        readonly property int warningWrapMode: warningText.wrapMode

                        Layout.fillWidth: true
                        spacing: 10

                        Icon {
                            Layout.preferredWidth: 18
                            Layout.preferredHeight: 18
                            Layout.alignment: Qt.AlignTop
                            source: "image://images/alert-filled"
                            color: Theme.color.orange
                            size: 18
                        }

                        CoreText {
                            id: warningText
                            objectName: "nodeWarningText_" + index
                            Layout.fillWidth: true
                            text: modelData
                            color: Theme.color.neutral8
                            font.pixelSize: 15
                            wrapMode: Text.WordWrap
                            horizontalAlignment: Text.AlignLeft
                        }
                    }
                }

                CoreText {
                    objectName: "nodeNoWarningsText"
                    Layout.fillWidth: true
                    visible: runtimeDialogModel.warningList.length === 0
                    text: qsTr("No current warnings.")
                    color: Theme.color.neutral7
                    font.pixelSize: 15
                }
            }
        }

        ContinueButton {
            objectName: "nodeWarningsCloseButton"
            Layout.fillWidth: true
            Layout.leftMargin: root.contentMargin
            Layout.rightMargin: root.contentMargin
            Layout.bottomMargin: root.contentMargin
            text: qsTr("OK")
            onClicked: root.close()
        }
    }
}
