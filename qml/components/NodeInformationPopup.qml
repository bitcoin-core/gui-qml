// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import org.bitcoincore.qt 1.0
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../controls"

DialogPopup {
    id: root
    objectName: "nodeInformationPopup"

    property var rows: ApplicationModels.nodeModel.informationRows
    readonly property int informationRowCount: informationRepeater.count
    readonly property string firstInformationValue: rows.length > 0 ? rows[0].value : ""
    readonly property int lastInformationValueLineCount: informationRepeater.count > 0 ? (informationRepeater.itemAt(informationRepeater.count - 1) as NodeInformationRow).valueLineCount : 0
    readonly property int lastInformationValueWrapMode: informationRepeater.count > 0 ? (informationRepeater.itemAt(informationRepeater.count - 1) as NodeInformationRow).valueWrapMode : Text.NoWrap


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
                        model: root.rows
                        delegate: NodeInformationRow { width: informationColumn.width; rowCount: root.rows.length }
                    }
                }
            }
        }
    }

}
