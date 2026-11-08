// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import org.bitcoincore.qt 1.0
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../controls"

Popup {
    id: root
    objectName: "nodeInformationPopup"

    property var rows: []
    readonly property int informationRowCount: informationRepeater.count
    readonly property string firstInformationValue: rows.length > 0 ? rows[0].value : ""
    readonly property int lastInformationValueLineCount: informationRepeater.count > 0 ? (informationRepeater.itemAt(informationRepeater.count - 1) as NodeInformationRow).valueLineCount : 0
    readonly property int lastInformationValueWrapMode: informationRepeater.count > 0 ? (informationRepeater.itemAt(informationRepeater.count - 1) as NodeInformationRow).valueWrapMode : Text.NoWrap
    readonly property int contentMargin: 28

    onAboutToShow: rows = ApplicationModels.nodeModel.nodeInformationRows()

    modal: true
    padding: 0
    anchors.centerIn: parent
    width: parent ? Math.min(parent.width - (2 * contentMargin), 760) : 760
    height: Math.min(implicitHeight, parent ? parent.height - 80 : 560)
    implicitHeight: Math.min(columnLayout.implicitHeight, 560)

    background: Rectangle {
        color: Theme.color.background
        radius: 8
        border.color: Theme.color.neutral4
        border.width: 1
        SurfaceGradientBorder {
            anchors.fill: parent
            surfaceColor: parent.color
            referenceColor: Theme.color.neutral1
            cornerRadius: parent.radius
        }
    }

    ColumnLayout {
        id: columnLayout
        anchors.fill: parent
        spacing: 0

        CoreText {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            text: qsTr("Node information")
            bold: true
            font.pixelSize: 20
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        Separator { Layout.fillWidth: true }

        ScrollView {
            id: informationScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: root.contentMargin
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
                    delegate: NodeInformationRow { width: informationColumn.width; rowCount: root.rows.length }
                }
            }
        }

        ContinueButton {
            objectName: "nodeInformationCloseButton"
            Layout.fillWidth: true
            Layout.leftMargin: root.contentMargin
            Layout.rightMargin: root.contentMargin
            Layout.bottomMargin: root.contentMargin
            text: qsTr("OK")
            onClicked: root.close()
        }
    }

}
