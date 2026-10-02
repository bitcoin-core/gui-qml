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

    onAboutToShow: {
        rows = nodeModel.nodeInformationRows()
        nodeModel.refreshNodeInformation()
    }
    onAboutToHide: rowsRefreshTimer.stop()
    // Node notifications arrive for every block and header during sync.
    // Read the rows at most twice a second while the popup is open.
    Timer {
        id: rowsRefreshTimer
        interval: 500
        onTriggered: root.rows = nodeModel.nodeInformationRows()
    }
    Connections {
        target: nodeModel
        function onNodeInformationChanged() {
            if (root.visible && !rowsRefreshTimer.running) rowsRefreshTimer.start()
        }
    }

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

            // The popup is surface1, so the rows sit on a surface2 card.
            FormSection {
                objectName: "nodeInformationSection"
                width: informationScroll.availableWidth
                surfaceLevel: 1

                // A Column positioner, never a Quick Layout: a Repeater inside one
                // crashes on Qt 6.4.0 to 6.5.0 (QTBUG-111792). See AlertPopup.qml.
                Column {
                    id: informationColumn
                    Layout.fillWidth: true
                    spacing: 0

                    Repeater {
                        id: informationRepeater
                        // A count keeps the rows when only their values change, so a
                        // refresh does not recreate them or drop a text selection.
                        model: root.rows.length
                        delegate: ValueRow {
                            required property int index
                            readonly property var row: root.rows[index] || ({})

                            width: informationColumn.width
                            title: row.label || ""
                            titleColor: Theme.color.neutral7
                            valueObjectName: "nodeInformationValue_" + index
                            value: row.value || ""
                            valueTextStyle: row.mono ? Theme.text.monoDescription : Theme.text.description
                            valueMaximumWidth: Math.round(informationColumn.width * 0.65)
                            valueWrap: true
                            showDivider: index < root.rows.length - 1
                        }
                    }
                }
            }
        }
    }
}
