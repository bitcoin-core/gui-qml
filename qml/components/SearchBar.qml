// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

import "../controls"

Control {
    id: root

    property alias text: searchField.text
    property alias placeholderText: searchField.placeholderText
    property alias inputField: searchField
    property string accessibleName: qsTr("Search")
    property bool showNavigationButtons: false
    property bool navigationEnabled: true
    property Item nextTabItem: null
    property string fieldObjectName: ""
    property string searchIconObjectName: ""
    property string clearButtonObjectName: ""
    property string navigationControlObjectName: ""
    property string previousButtonObjectName: ""
    property string nextButtonObjectName: ""
    readonly property alias navigationControl: searchNavigation
    readonly property alias previousNavigationButton: previousButton
    readonly property alias nextNavigationButton: nextButton

    signal previousRequested()
    signal nextRequested()

    function focusSearch() {
        searchField.forceActiveFocus()
    }

    function selectAll() {
        searchField.selectAll()
    }

    implicitWidth: showNavigationButtons ? 416 : 340
    implicitHeight: showNavigationButtons ? 48 : 40
    padding: showNavigationButtons ? 4 : 0

    background: Rectangle {
        visible: root.showNavigationButtons
        color: Theme.color.neutral1
        radius: 8
    }

    contentItem: RowLayout {
        spacing: 2

        TextField {
            id: searchField

            objectName: root.fieldObjectName
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 64
            leftPadding: 32
            rightPadding: clearButton.visible ? 28 : 10
            topPadding: 0
            bottomPadding: 0
            placeholderTextColor: Theme.color.neutral7
            color: Theme.color.neutral9
            font: Theme.text.caption.font
            verticalAlignment: TextInput.AlignVCenter
            selectByMouse: true
            Accessible.name: root.accessibleName
            KeyNavigation.tab: root.showNavigationButtons
                ? previousButton
                : root.nextTabItem

            Keys.onReturnPressed: function(event) {
                if (!root.showNavigationButtons || !root.navigationEnabled) return
                if (event.modifiers & Qt.ShiftModifier) {
                    root.previousRequested()
                } else {
                    root.nextRequested()
                }
                event.accepted = true
            }

            background: Rectangle {
                color: Theme.color.neutral2
                radius: 5
                border.width: searchField.activeFocus ? 2 : 0
                border.color: Theme.color.orange
            }

            Icon {
                objectName: root.searchIconObjectName
                anchors.left: parent.left
                anchors.leftMargin: 9
                anchors.verticalCenter: parent.verticalCenter
                source: "image://images/search"
                color: Theme.color.neutral7
                size: 14
                hoverEnabled: false
            }

            AbstractButton {
                id: clearButton

                readonly property color clearColor: Theme.color.neutral4

                objectName: root.clearButtonObjectName
                anchors.right: parent.right
                anchors.rightMargin: 7
                anchors.verticalCenter: parent.verticalCenter
                width: 14
                height: 14
                padding: 0
                visible: searchField.text.length > 0
                hoverEnabled: AppMode.isDesktop
                focusPolicy: Qt.NoFocus
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Clear search")

                background: Rectangle {
                    color: clearButton.hovered || clearButton.pressed
                        ? Theme.color.neutral3
                        : "transparent"
                    border.width: 1
                    border.color: clearButton.clearColor
                    radius: width / 2
                }

                contentItem: ClearSearchIcon {
                    strokeColor: clearButton.clearColor
                }

                HoverHandler {
                    cursorShape: Qt.PointingHandCursor
                }

                onClicked: {
                    searchField.clear()
                    searchField.forceActiveFocus()
                }
            }
        }

        Control {
            id: searchNavigation

            objectName: root.navigationControlObjectName
            visible: root.showNavigationButtons
            implicitWidth: 54
            implicitHeight: 40
            Layout.minimumWidth: 54
            Layout.preferredWidth: 54
            Layout.maximumWidth: 54
            Layout.fillHeight: true
            padding: 0
            focusPolicy: Qt.NoFocus

            Accessible.role: Accessible.Grouping
            Accessible.name: qsTr("Search result navigation")
            background: null

            contentItem: RowLayout {
                spacing: 2

                SearchNavigationButton {
                    id: previousButton

                    objectName: root.previousButtonObjectName
                    enabled: root.navigationEnabled
                    accessibleName: qsTr("Previous search result")
                    rotationAngle: -90
                    KeyNavigation.tab: nextButton
                    KeyNavigation.backtab: searchField
                    onClicked: root.previousRequested()
                }

                SearchNavigationButton {
                    id: nextButton

                    objectName: root.nextButtonObjectName
                    enabled: root.navigationEnabled
                    accessibleName: qsTr("Next search result")
                    rotationAngle: 90
                    KeyNavigation.tab: root.nextTabItem
                    KeyNavigation.backtab: previousButton
                    onClicked: root.nextRequested()
                }
            }
        }
    }

    component SearchNavigationButton: AbstractButton {
        id: navigationButton

        required property string accessibleName
        required property real rotationAngle

        implicitWidth: 26
        implicitHeight: 40
        Layout.minimumWidth: 26
        Layout.preferredWidth: 26
        Layout.maximumWidth: 26
        Layout.fillHeight: true
        padding: 0
        hoverEnabled: AppMode.isDesktop
        focusPolicy: Qt.TabFocus
        Accessible.role: Accessible.Button
        Accessible.name: accessibleName

        background: Rectangle {
            color: navigationButton.hovered || navigationButton.pressed
                ? Theme.color.neutral2
                : "transparent"
            radius: 5
        }

        contentItem: Item {
            SearchNavigationCaret {
                objectName: navigationButton.objectName.length > 0
                    ? navigationButton.objectName + "Icon"
                    : ""
                anchors.centerIn: parent
                width: 14
                height: 14
                strokeColor: navigationButton.enabled
                    ? Theme.color.neutral8
                    : Theme.color.neutral4
                rotation: navigationButton.rotationAngle
            }
        }

        FocusBorder {
            objectName: navigationButton.objectName.length > 0
                ? navigationButton.objectName + "FocusBorder"
                : ""
            visible: navigationButton.activeFocus
            borderRadius: 9
            z: 1
        }

        HoverHandler {
            cursorShape: navigationButton.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        }
    }

    component SearchNavigationCaret: Canvas {
        id: caret

        required property color strokeColor
        readonly property real strokeWidth: 2

        antialiasing: true

        onPaint: {
            const context = getContext("2d")
            context.clearRect(0, 0, width, height)
            context.strokeStyle = strokeColor
            context.lineWidth = strokeWidth
            context.lineCap = "round"
            context.lineJoin = "round"
            context.beginPath()
            context.moveTo(4.5, 2.75)
            context.lineTo(9.5, 7)
            context.lineTo(4.5, 11.25)
            context.stroke()
        }

        onStrokeColorChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
    }

    component ClearSearchIcon: Canvas {
        id: clearIcon

        required property color strokeColor
        readonly property real size: 6
        readonly property real strokeWidth: 1.5

        antialiasing: true

        onPaint: {
            const context = getContext("2d")
            const centerX = width / 2
            const centerY = height / 2
            const halfSize = size / 2
            context.clearRect(0, 0, width, height)
            context.strokeStyle = strokeColor
            context.lineWidth = strokeWidth
            context.lineCap = "round"
            context.beginPath()
            context.moveTo(centerX - halfSize, centerY - halfSize)
            context.lineTo(centerX + halfSize, centerY + halfSize)
            context.moveTo(centerX + halfSize, centerY - halfSize)
            context.lineTo(centerX - halfSize, centerY + halfSize)
            context.stroke()
        }

        onStrokeColorChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
    }
}
