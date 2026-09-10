// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Controls.impl 2.15 as ControlsImpl
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

import "../controls"

Control {
    id: root

    property alias text: searchField.text
    property alias placeholderText: searchField.placeholderText
    property alias placeholder: searchField.placeholderText
    property alias inputField: searchField
    readonly property alias cancelButton: clearButton
    property string accessibleName: qsTr("Search")
    property string cancelAccessibleName: qsTr("Clear search")
    property bool showsSearchIcon: true
    property bool showsCancel: true
    property bool clearsOnCancel: true
    property bool refocusesOnCancel: true
    property bool showNavigationButtons: false
    property bool navigationEnabled: true
    property url searchIconSource: "image://images/search"
    property url cancelIconSource: "qrc:/icons/cross-circle-filled"
    property int searchIconSize: 14
    property int cancelButtonSize: 14
    property color cancelIconColor: Theme.color.neutral6
    property color cancelIconHoverColor: Theme.color.neutral5
    property color cancelIconPressedColor: Theme.color.neutral4
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
    signal queryEdited(string query)
    signal searchRequested(string query)
    signal cancelRequested()

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
            leftPadding: root.showsSearchIcon ? 32 : 10
            rightPadding: clearButton.visible ? root.cancelButtonSize + 14 : 10
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
                if (root.showNavigationButtons && root.navigationEnabled) {
                    if (event.modifiers & Qt.ShiftModifier) {
                        root.previousRequested()
                    } else {
                        root.nextRequested()
                    }
                } else {
                    root.searchRequested(searchField.text)
                }
                event.accepted = true
            }

            onTextEdited: root.queryEdited(text)

            background: Rectangle {
                color: Theme.color.neutral2
                radius: 5
            }

            Icon {
                objectName: root.searchIconObjectName
                anchors.left: parent.left
                anchors.leftMargin: 9
                anchors.verticalCenter: parent.verticalCenter
                visible: root.showsSearchIcon
                source: root.searchIconSource
                color: Theme.color.neutral7
                size: root.searchIconSize
                hoverEnabled: false
            }

            AbstractButton {
                id: clearButton

                objectName: root.clearButtonObjectName
                anchors.right: parent.right
                anchors.rightMargin: 7
                anchors.verticalCenter: parent.verticalCenter
                width: root.cancelButtonSize
                height: root.cancelButtonSize
                padding: 0
                visible: root.showsCancel && searchField.text.length > 0
                hoverEnabled: AppMode.isDesktop
                focusPolicy: Qt.NoFocus
                Accessible.role: Accessible.Button
                Accessible.name: root.cancelAccessibleName

                background: null

                contentItem: Item {
                    readonly property url source: root.cancelIconSource
                    readonly property color color: clearButton.pressed
                        ? root.cancelIconPressedColor
                        : clearButton.hovered
                            ? root.cancelIconHoverColor
                            : root.cancelIconColor
                    readonly property int size: Math.max(1, root.cancelButtonSize - 2)

                    ControlsImpl.IconImage {
                        anchors.centerIn: parent
                        width: parent.size
                        height: parent.size
                        source: parent.source
                        color: parent.color
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                        mipmap: true
                    }
                }

                HoverHandler {
                    cursorShape: Qt.PointingHandCursor
                }

                onClicked: {
                    root.cancelRequested()
                    if (root.clearsOnCancel) searchField.clear()
                    if (root.refocusesOnCancel) searchField.forceActiveFocus()
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

}
