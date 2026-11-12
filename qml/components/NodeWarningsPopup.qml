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
    objectName: "nodeWarningsPopup"

    readonly property int warningCount: warningRepeater.count
    readonly property string firstWarningText: ApplicationModels.nodeModel.warningList.length > 0 ? ApplicationModels.nodeModel.warningList[0] : ""
    readonly property int firstWarningLineCount: warningRepeater.count > 0 ? (warningRepeater.itemAt(0) as NodeWarningRow).warningLineCount : 0
    readonly property int firstWarningWrapMode: warningRepeater.count > 0 ? (warningRepeater.itemAt(0) as NodeWarningRow).warningWrapMode : Text.NoWrap
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

            // A Column positioner, never a Quick Layout: a Repeater inside one
            // crashes on Qt 6.4.0 to 6.5.0 (QTBUG-111792). See AlertPopup.qml.
            Column {
                id: warningsColumn
                width: warningsScroll.availableWidth
                spacing: 12

                Repeater {
                    id: warningRepeater
                    model: ApplicationModels.nodeModel.warningList
                    delegate: NodeWarningRow { width: warningsColumn.width }
                }

                CoreText {
                    objectName: "nodeNoWarningsText"
                    width: warningsColumn.width
                    visible: ApplicationModels.nodeModel.warningList.length === 0
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
