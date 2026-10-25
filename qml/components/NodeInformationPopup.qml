// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../controls"

DialogPopup {
    id: root
    objectName: "nodeInformationPopup"

    property var rows: []
    readonly property int informationRowCount: informationRepeater.count
    readonly property string firstInformationValue: rows.length > 0 ? rows[0].value : ""
    readonly property int lastInformationValueLineCount: informationRepeater.count > 0 ? informationRepeater.itemAt(informationRepeater.count - 1).valueLineCount : 0
    readonly property int lastInformationValueWrapMode: informationRepeater.count > 0 ? informationRepeater.itemAt(informationRepeater.count - 1).valueWrapMode : Text.NoWrap

    onAboutToShow: rows = nodeModel.nodeInformationRows()

    title: qsTr("Node information")
    implicitWidth: 760

    contentItem: ColumnLayout {
        spacing: Theme.space.xl

        DialogHeader {
            Layout.fillWidth: true
            title: root.title
            closeButtonObjectName: "nodeInformationCloseButton"
            onCloseRequested: root.close()
        }

        ScrollView {
            id: informationScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true

            // A Column positioner, never a Quick Layout: a Repeater inside one
            // crashes on Qt 6.4.0 to 6.5.0 (QTBUG-111792). See AlertPopup.qml.
            Column {
                id: informationColumn
                width: informationScroll.availableWidth
                spacing: 0

                Repeater {
                    id: informationRepeater
                    model: root.rows
                    delegate: ColumnLayout {
                        readonly property int valueLineCount: informationValue.lineCount
                        readonly property int valueWrapMode: informationValue.wrapMode

                        width: informationColumn.width
                        spacing: 0

                        KeyValueRow {
                            Layout.fillWidth: true
                            keyWidth: 150
                            key: CoreText {
                                text: modelData.label
                                color: Theme.color.neutral7
                                font.pixelSize: 13
                                horizontalAlignment: Text.AlignLeft
                                verticalAlignment: Text.AlignTop
                                wrap: false
                            }
                            value: CoreText {
                                id: informationValue
                                objectName: "nodeInformationValue_" + index
                                text: modelData.value
                                color: Theme.color.neutral9
                                font.pixelSize: 13
                                wrapMode: Text.WordWrap
                                horizontalAlignment: Text.AlignLeft
                                verticalAlignment: Text.AlignTop
                            }
                        }

                        Separator {
                            Layout.fillWidth: true
                            Layout.topMargin: 10
                            Layout.bottomMargin: 10
                            visible: index < root.rows.length - 1
                        }
                    }
                }
            }
        }
    }
}
