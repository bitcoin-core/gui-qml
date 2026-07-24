// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQml 2.15
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../controls"

Popup {
    id: root

    property string title: ""
    property string message: ""
    property string messageObjectName: "alertMessage"
    default property alias actions: actionStore.data

    property var visibleActions: [defaultAction]

    modal: true
    padding: 0
    anchors.centerIn: parent
    width: parent ? Math.min(parent.width - 40, 360) : 360
    implicitHeight: columnLayout.implicitHeight

    property Item actionStoreItem: Item {
        id: actionStore
        visible: false
    }
    property AlertAction defaultAction: AlertAction { text: qsTr("OK") }

    function refreshActions() {
        visibleActions = actionStore.data.length > 0 ? actionStore.data : [defaultAction]
    }

    Component.onCompleted: refreshActions()
    onOpened: refreshActions()

    background: Rectangle {
        color: Theme.color.background
        radius: 8
        border.color: Theme.color.neutral4
        border.width: 1
    }

    contentItem: ColumnLayout {
        id: columnLayout
        spacing: 0

        CoreText {
            objectName: "alertTitle"
            Layout.fillWidth: true
            Layout.preferredHeight: 64
            text: root.title
            font: Theme.text.subtitle.font
            lineHeight: Theme.text.subtitle.lineHeight
            lineHeightMode: Text.FixedHeight
            color: Theme.color.neutral9
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        Separator {
            Layout.fillWidth: true
        }

        CoreText {
            objectName: root.messageObjectName
            Layout.fillWidth: true
            Layout.margins: 20
            text: root.message
            color: Theme.color.neutral8
            font: Theme.text.description.font
            lineHeight: Theme.text.description.lineHeight
            lineHeightMode: Text.FixedHeight
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
        }

        // The action buttons live in a Row positioner, not a RowLayout, on
        // purpose. A Repeater directly inside a Quick Layout hits a Qt 6.4
        // use-after-free in QGridLayoutEngine when the layout rearranges while
        // the Repeater is rebuilding a delegate (for example, navigating to the
        // Send tab resizes the popup, which re-fires its parent-bound width and
        // rearranges this row). A positioner never hands delegates to the grid
        // layout engine, so the buttons are given equal widths explicitly.
        Row {
            id: actionRow
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.bottomMargin: 20
            spacing: 10

            Repeater {
                model: root.visibleActions.length

                ContinueButton {
                    id: alertButton
                    // Qt 6.2: does not inject `index` into this delegate; declare
                    // it explicitly so `visibleActions[index]` resolves.
                    required property int index

                    readonly property AlertAction alertAction: root.visibleActions[index]

                    objectName: alertAction.buttonObjectName
                    width: Math.max(0, (actionRow.width - actionRow.spacing * (root.visibleActions.length - 1)) / root.visibleActions.length)
                    text: alertAction.text
                    textColor: alertAction.role === AlertAction.Cancel ? Theme.color.neutral9 : Theme.color.white
                    textHoverColor: textColor
                    textPressedColor: textColor
                    backgroundColor: alertAction.role === AlertAction.Cancel
                        ? Theme.color.background
                        : alertAction.role === AlertAction.Destructive ? Theme.color.red : Theme.color.orange
                    backgroundHoverColor: alertAction.role === AlertAction.Cancel
                        ? Theme.color.background
                        : alertAction.role === AlertAction.Destructive ? Qt.lighter(Theme.color.red, 1.1) : Theme.color.orangeLight1
                    backgroundPressedColor: alertAction.role === AlertAction.Cancel
                        ? Theme.color.neutral2
                        : alertAction.role === AlertAction.Destructive ? Qt.darker(Theme.color.red, 1.1) : Theme.color.orangeLight2
                    borderColor: alertAction.role === AlertAction.Cancel ? Theme.color.neutral6 : "transparent"
                    borderHoverColor: alertAction.role === AlertAction.Cancel ? Theme.color.neutral9 : "transparent"
                    borderPressedColor: alertAction.role === AlertAction.Cancel ? Theme.color.neutral2 : "transparent"
                    onClicked: {
                        if (alertAction.closesPopup) {
                            root.close()
                        }
                        alertAction.triggered()
                    }
                }
            }
        }
    }
}
