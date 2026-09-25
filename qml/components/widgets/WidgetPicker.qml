// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../controls"

Popup {
    id: root
    required property var layoutModel
    property int layoutRevision: 0
    property string addError: ""
    property bool reducedMotion: false
    property real verticalOffset: 0
    readonly property real centeredY: parent ? Math.max(20, (parent.height - height) / 2) : 0
    readonly property var sortedCatalog: layoutModel.catalog.slice().sort(function(a, b) { return a.title.localeCompare(b.title) })
    objectName: "widgetPicker"
    modal: true
    dim: true
    focus: true
    width: Math.min(560, parent ? parent.width - 32 : 560)
    height: Math.min(contentColumn.implicitHeight + topPadding + bottomPadding, parent ? parent.height - 40 : 560)
    padding: width < 480 ? 16 : 24
    x: parent ? (parent.width - width) / 2 : 0
    y: centeredY + verticalOffset
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onAboutToShow: {
        addError = ""
        if (scroll.contentItem) scroll.contentItem.contentY = 0
    }
    background: Rectangle {
        color: Theme.color.neutral1
        radius: 16
        border.color: Theme.color.neutral3
        SurfaceGradientBorder { anchors.fill: parent; surfaceColor: parent.color; cornerRadius: parent.radius }
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
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 150 } }
    Connections {
        target: root.layoutModel
        function onLayoutChanged() { root.layoutRevision++ }
    }
    contentItem: ColumnLayout {
        id: contentColumn
        spacing: 16
        RowLayout {
            Layout.fillWidth: true
            Layout.bottomMargin: 8
            spacing: 10
            CoreText {
                objectName: "widgetPickerTitle"
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignLeft
                font: Theme.text.title.font
                text: qsTr("Add widgets")
            }
            CloseButton {
                objectName: "widgetPickerClose"
                iconColor: Theme.color.neutral7
                Accessible.name: qsTr("Close widget picker")
                onClicked: root.close()
            }
        }
        CoreText {
            Layout.fillWidth: true
            visible: root.addError.length > 0
            text: root.addError
            font: Theme.text.caption.font
            color: Theme.color.neutral7
            horizontalAlignment: Text.AlignLeft
        }
        ScrollView {
            id: scroll
            objectName: "widgetPickerScroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
            implicitHeight: table.implicitHeight
            contentWidth: availableWidth
            contentHeight: table.implicitHeight
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            FormSection {
                id: table
                objectName: "widgetPickerTable"
                width: scroll.availableWidth
                isOnSurface: true
                showGradientBorder: false
                Repeater {
                    model: root.sortedCatalog
                    delegate: FormRow {
                        id: choice
                        required property var modelData
                        required property int index
                        objectName: "widgetPickerRow_" + modelData.id
                        readonly property bool added: root.layoutRevision >= 0 && root.layoutModel.contains(modelData.id)
                        Layout.fillWidth: true
                        title: modelData.title
                        minimumRowHeight: 64
                        dividerColor: Theme.color.neutral3
                        showDivider: index < root.sortedCatalog.length - 1
                        trailingItem: NeutralButton {
                            objectName: "widgetPickerAdd_" + choice.modelData.id
                            buttonSize: NeutralButton.Medium
                            enabled: !choice.added
                            text: choice.added ? qsTr("Added") : qsTr("Add")
                            onClicked: {
                                if (root.layoutModel.addWidget(choice.modelData.id)) {
                                    root.close()
                                } else {
                                    root.addError = qsTr("There is not enough room for this widget. Resize or remove an existing widget to make room.")
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
