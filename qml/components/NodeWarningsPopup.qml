// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../controls"

DialogPopup {
    id: root
    objectName: "nodeWarningsPopup"

    readonly property int warningCount: warningRepeater.count
    readonly property string firstWarningText: nodeModel.warningList.length > 0 ? nodeModel.warningList[0] : ""
    readonly property int firstWarningLineCount: warningRepeater.count > 0 ? warningRepeater.itemAt(0).warningLineCount : 0
    readonly property int firstWarningWrapMode: warningRepeater.count > 0 ? warningRepeater.itemAt(0).warningWrapMode : Text.NoWrap

    title: qsTr("Node warnings")
    implicitWidth: 720

    contentItem: ColumnLayout {
        spacing: Theme.space.xl

        DialogHeader {
            Layout.fillWidth: true
            title: root.title
            closeButtonObjectName: "nodeWarningsCloseButton"
            onCloseRequested: root.close()
        }

        ScrollView {
            id: warningsScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true

            // A Column positioner, never a Quick Layout: a Repeater inside one
            // crashes on Qt 6.4.0 to 6.5.0 (QTBUG-111792). See AlertPopup.qml.
            Column {
                id: warningsColumn
                width: warningsScroll.availableWidth
                spacing: 12

                Repeater {
                    id: warningRepeater
                    model: nodeModel.warningList
                    delegate: RowLayout {
                        // Qt 6.2 does not inject `modelData`/`index` into this
                        // delegate; declare them explicitly.
                        required property var modelData
                        required property int index

                        readonly property int warningLineCount: warningText.lineCount
                        readonly property int warningWrapMode: warningText.wrapMode

                        width: warningsColumn.width
                        spacing: Theme.space.ml

                        Icon {
                            Layout.preferredWidth: Theme.size.icon.l
                            Layout.preferredHeight: Theme.size.icon.l
                            Layout.alignment: Qt.AlignTop
                            source: "image://images/alert-filled"
                            color: Theme.color.warning
                            size: Theme.size.icon.l
                        }

                        CoreText {
                            id: warningText
                            objectName: "nodeWarningText_" + index
                            Layout.fillWidth: true
                            text: modelData
                            color: Theme.color.neutral8
                            textStyle: Theme.text.description
                            wrapMode: Text.WordWrap
                            horizontalAlignment: Text.AlignLeft
                        }
                    }
                }

                CoreText {
                    objectName: "nodeNoWarningsText"
                    width: warningsColumn.width
                    visible: nodeModel.warningList.length === 0
                    text: qsTr("No current warnings.")
                    color: Theme.color.neutral7
                    textStyle: Theme.text.description
                    horizontalAlignment: Text.AlignLeft
                }
            }
        }
    }
}
