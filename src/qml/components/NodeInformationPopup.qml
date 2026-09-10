// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../controls"

Popup {
    id: root
    objectName: "nodeInformationPopup"

    property var rows: []
    readonly property int informationRowCount: informationRepeater.count
    readonly property string firstInformationValue: rows.length > 0 ? rows[0].value : ""
    readonly property int lastInformationValueLineCount: informationRepeater.count > 0 ? informationRepeater.itemAt(informationRepeater.count - 1).valueLineCount : 0
    readonly property int lastInformationValueWrapMode: informationRepeater.count > 0 ? informationRepeater.itemAt(informationRepeater.count - 1).valueWrapMode : Text.NoWrap
    readonly property int contentMargin: 28

    onAboutToShow: rows = nodeInformationModel.nodeInformationRows()

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

            ColumnLayout {
                width: informationScroll.availableWidth
                spacing: 0

                Repeater {
                    id: informationRepeater
                    model: root.rows
                    delegate: ColumnLayout {
                        readonly property int valueLineCount: informationValue.lineCount
                        readonly property int valueWrapMode: informationValue.wrapMode

                        width: parent.width
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
