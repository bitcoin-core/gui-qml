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
    property real verticalOffset: 0
    default property alias actions: actionStore.data

    property var visibleActions: [defaultAction]
    readonly property real minimumActionWidth: visibleActions.reduce(function(width, action) {
        return Math.max(width, Math.ceil(actionFontMetrics.advanceWidth(action.text)) + 40)
    }, 0)
    readonly property real horizontalActionsWidth: minimumActionWidth * visibleActions.length
        + 10 * Math.max(0, visibleActions.length - 1)

    property FontMetrics actionMetrics: FontMetrics {
        id: actionFontMetrics
        font: Theme.text.buttonStrong.font
    }

    modal: true
    dim: true
    focus: true
    closePolicy: Popup.NoAutoClose
    padding: 0
    implicitWidth: Math.max(360, horizontalActionsWidth + 40)
    width: parent ? Math.min(parent.width - 40, implicitWidth) : implicitWidth
    implicitHeight: columnLayout.implicitHeight
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) + verticalOffset : verticalOffset

    Overlay.modal: Rectangle {
        objectName: "alertPopupDimmer"
        color: Qt.rgba(0, 0, 0, 0.5)
    }

    enter: Transition {
        NumberAnimation {
            property: "opacity"
            from: 0
            to: 1
            duration: 300
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            property: "verticalOffset"
            from: -30
            to: 0
            duration: 300
            easing.type: Easing.OutCubic
        }
    }

    exit: Transition {
        NumberAnimation {
            property: "opacity"
            from: 1
            to: 0
            duration: 250
            easing.type: Easing.InCubic
        }
        NumberAnimation {
            property: "verticalOffset"
            from: 0
            to: -30
            duration: 250
            easing.type: Easing.InCubic
        }
    }

    property Item actionStoreItem: Item {
        id: actionStore
        visible: false
    }
    property AlertAction defaultAction: AlertAction { text: qsTr("OK") }

    function refreshActions() {
        visibleActions = actionStore.data.length > 0 ? actionStore.data : [defaultAction]
    }

    function cancel() {
        for (let i = 0; i < visibleActions.length; ++i) {
            const action = visibleActions[i]
            if (action.role === AlertAction.Cancel || action.role === AlertAction.Neutral) {
                close()
                action.triggered()
                return
            }
        }
        close()
    }

    Component.onCompleted: refreshActions()
    onOpened: {
        refreshActions()
        columnLayout.forceActiveFocus()
    }

    background: Rectangle {
        objectName: "alertPopupSurface"
        color: Theme.color.neutral1
        radius: 10
        border.color: Theme.color.neutral2
        border.width: 1
        SurfaceGradientBorder {
            anchors.fill: parent
            surfaceColor: parent.color
            cornerRadius: parent.radius
        }
    }

    contentItem: ColumnLayout {
        id: columnLayout
        focus: true
        spacing: 0

        Keys.priority: Keys.BeforeItem
        Keys.onEscapePressed: function(event) {
            root.cancel()
            event.accepted = true
        }

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
            color: Theme.color.neutral2
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

        // The action buttons live in a Flow positioner, not a RowLayout, on
        // purpose. A Repeater directly inside a Quick Layout hits a Qt 6.4
        // use-after-free in QGridLayoutEngine when the layout rearranges while
        // the Repeater is rebuilding a delegate (for example, navigating to the
        // Send tab resizes the popup, which re-fires its parent-bound width and
        // rearranges this row). A positioner never hands delegates to the grid
        // layout engine. Keep equal widths and stack actions when their full
        // labels cannot fit side by side in the available space.
        Flow {
            id: actionRow
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.bottomMargin: 20
            spacing: 10
            readonly property bool stacked: width < root.horizontalActionsWidth

            Repeater {
                model: root.visibleActions.length

                Item {
                    id: actionDelegate
                    // Qt 6.2: does not inject `index` into this delegate; declare
                    // it explicitly so `visibleActions[index]` resolves.
                    required property int index

                    readonly property AlertAction alertAction: root.visibleActions[index]
                    readonly property bool neutralAction: alertAction.role === AlertAction.Cancel
                        || alertAction.role === AlertAction.Neutral

                    width: actionRow.stacked ? actionRow.width
                        : Math.max(0, (actionRow.width - actionRow.spacing * (root.visibleActions.length - 1)) / root.visibleActions.length)
                    height: 46

                    function triggerAction() {
                        if (alertAction.closesPopup) {
                            root.close()
                        }
                        alertAction.triggered()
                    }

                    NeutralButton {
                        anchors.fill: parent
                        visible: actionDelegate.neutralAction
                        objectName: visible ? actionDelegate.alertAction.buttonObjectName : ""
                        text: actionDelegate.alertAction.text
                        buttonSize: NeutralButton.Large
                        backgroundColor: Theme.color.neutral2
                        hoverBackgroundColor: Theme.color.neutral3
                        onClicked: actionDelegate.triggerAction()
                    }

                    ContinueButton {
                        anchors.fill: parent
                        horizontalPadding: 20
                        visible: !actionDelegate.neutralAction
                        objectName: visible ? actionDelegate.alertAction.buttonObjectName : ""
                        text: actionDelegate.alertAction.text
                        backgroundColor: actionDelegate.alertAction.role === AlertAction.Destructive
                            ? Theme.color.red : Theme.color.orange
                        backgroundHoverColor: actionDelegate.alertAction.role === AlertAction.Destructive
                            ? Qt.lighter(Theme.color.red, 1.1) : Theme.color.orangeLight1
                        backgroundPressedColor: actionDelegate.alertAction.role === AlertAction.Destructive
                            ? Qt.darker(Theme.color.red, 1.1) : Theme.color.orangeLight2
                        onClicked: actionDelegate.triggerAction()
                    }
                }
            }
        }
    }
}
