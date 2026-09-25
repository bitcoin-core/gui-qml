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
    property bool reducedMotion: false
    property real verticalOffset: 0
    readonly property int informationRowCount: informationRepeater.count
    readonly property string firstInformationValue: rows.length > 0 ? rows[0].value : ""
    readonly property int lastInformationValueLineCount: informationRepeater.count > 0
        ? informationRepeater.itemAt(informationRepeater.count - 1).valueLineCount
        : 0
    readonly property int lastInformationValueWrapMode: informationRepeater.count > 0
        ? informationRepeater.itemAt(informationRepeater.count - 1).valueWrapMode
        : Text.NoWrap
    readonly property int contentMargin: width < 480 ? 16 : 24
    readonly property real centeredY: parent ? Math.max(20, (parent.height - height) / 2) : 0
    readonly property real maximumTableHeight: parent
        ? Math.max(160, parent.height - 190 - (nodeModel.hasWarnings ? warningBanner.implicitHeight + 16 : 0))
        : 620

    function refreshRows() {
        rows = nodeModel.nodeInformationRows().filter(function(row) {
            return row.id !== "warnings"
        })
    }

    onAboutToShow: {
        refreshRows()
        if (informationScroll.contentItem) informationScroll.contentItem.contentY = 0
    }

    width: Math.min(560, parent ? parent.width - 32 : 560)
    height: Math.min(implicitHeight, parent ? parent.height - 40 : implicitHeight)
    implicitHeight: contentColumn.implicitHeight + topPadding + bottomPadding
    x: parent ? (parent.width - width) / 2 : 0
    y: centeredY + verticalOffset
    padding: contentMargin
    modal: true
    dim: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        objectName: "nodeInformationSurface"
        color: Theme.color.neutral1
        radius: 16
        border.width: 0
    }

    Overlay.modal: Rectangle {
        color: Qt.rgba(0, 0, 0, 0.6)
        Behavior on opacity { NumberAnimation { duration: 180 } }
    }

    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 180 }
            NumberAnimation {
                property: "verticalOffset"
                from: root.reducedMotion ? 0 : 16
                to: 0
                duration: root.reducedMotion ? 0 : 280
                easing.type: Easing.OutCubic
            }
        }
    }

    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 150 }
    }

    contentItem: ColumnLayout {
        id: contentColumn
        spacing: 16

        RowLayout {
            Layout.fillWidth: true
            Layout.bottomMargin: 8
            spacing: 10

            CoreText {
                objectName: "nodeInformationTitle"
                Layout.fillWidth: true
                text: qsTr("Node information")
                font: Theme.text.title.font
                horizontalAlignment: Text.AlignLeft
                wrap: true
            }

            CloseButton {
                objectName: "nodeInformationCloseButton"
                iconColor: Theme.color.neutral7
                Accessible.name: qsTr("Close node information")
                onClicked: root.close()
            }
        }

        ToastBanner {
            id: warningBanner
            objectName: "nodeInformationWarningBanner"
            Layout.fillWidth: true
            visible: nodeModel.hasWarnings
            iconSource: "image://images/alert-filled"
            iconColor: Theme.color.red
            textColor: Theme.color.neutral9
            backgroundColor: Qt.rgba(Theme.color.red.r,
                                     Theme.color.red.g,
                                     Theme.color.red.b,
                                     0.12)
            textObjectName: "nodeInformationWarningText"
            text: nodeModel.warningList.join("\n")
        }

        ScrollView {
            id: informationScroll
            objectName: "nodeInformationScroll"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(informationTable.implicitHeight, root.maximumTableHeight)
            implicitHeight: Layout.preferredHeight
            contentWidth: availableWidth
            contentHeight: informationTable.implicitHeight
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            Rectangle {
                id: informationTable
                objectName: "nodeInformationTable"
                width: informationScroll.availableWidth
                implicitHeight: tableColumn.implicitHeight
                color: Theme.color.neutral2
                radius: 12
                border.width: 0
                clip: true

                // Keep the Repeater inside a Column positioner. A Repeater in a
                // Quick Layout crashes on Qt 6.4.0 to 6.5.0 (QTBUG-111792).
                Column {
                    id: tableColumn
                    width: parent.width
                    spacing: 0

                    Repeater {
                        id: informationRepeater
                        model: root.rows

                        delegate: Column {
                            id: rowDelegate
                            required property var modelData
                            required property int index
                            readonly property int valueLineCount: informationValue.lineCount
                            readonly property int valueWrapMode: informationValue.wrapMode
                            width: tableColumn.width

                            RowLayout {
                                x: root.width < 480 ? 14 : 20
                                width: parent.width - (2 * x)
                                height: Math.max(48, Math.max(informationKey.implicitHeight,
                                                             informationValue.implicitHeight) + 20)
                                spacing: 16

                                CoreText {
                                    id: informationKey
                                    Layout.preferredWidth: root.width < 480 ? 112 : 150
                                    Layout.alignment: Qt.AlignVCenter
                                    text: rowDelegate.modelData.label
                                    font: Theme.text.description.font
                                    color: Theme.color.neutral6
                                    horizontalAlignment: Text.AlignLeft
                                    verticalAlignment: Text.AlignVCenter
                                    wrap: false
                                }

                                CoreText {
                                    id: informationValue
                                    objectName: "nodeInformationValue_" + rowDelegate.index
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    Layout.alignment: Qt.AlignVCenter
                                    text: rowDelegate.modelData.value
                                    font: Theme.text.description.font
                                    color: Theme.color.neutral8
                                    horizontalAlignment: Text.AlignLeft
                                    verticalAlignment: Text.AlignVCenter
                                    wrapMode: Text.WordWrap
                                }
                            }

                            Separator {
                                x: root.width < 480 ? 14 : 20
                                width: parent.width - (2 * x)
                                visible: rowDelegate.index < root.rows.length - 1
                                color: Theme.color.neutral3
                            }
                        }
                    }
                }
            }
        }
    }
}
