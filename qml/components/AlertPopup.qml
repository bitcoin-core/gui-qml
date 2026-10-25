// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQml 2.15
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../controls"

DialogPopup {
    id: root

    enum Tone {
        Default,
        Success,
        Warning,
        Error
    }

    property string titleObjectName: "alertTitle"
    property string message: ""
    property string messageObjectName: "alertMessage"
    property int messageTextFormat: Text.AutoText
    property int messageWrapMode: Text.WordWrap
    property int messageAlignment: Text.AlignHCenter
    property int tone: AlertPopup.Default
    // Success shows a check on a filled badge; other tones tint the icon.
    property url iconSource: tone === AlertPopup.Success ? "qrc:/icons/check.svg" : ""
    readonly property color toneColor: tone === AlertPopup.Success ? Theme.color.success
        : tone === AlertPopup.Warning ? Theme.color.warning
        : tone === AlertPopup.Error ? Theme.color.danger
        : Theme.color.neutral9
    property string errorText: ""
    property string errorTextObjectName: "alertErrorText"
    // Extra controls between the message and the actions, such as a field.
    property alias content: contentColumn.data
    default property alias actions: actionStore.data
    // Replaces the declared actions, for actions built from data.
    property var actionList: null
    // Answered by Escape; defaults to the first Cancel or Neutral action.
    property AlertAction escapeAction: null
    // Answered by Return outside a button; defaults to the only Normal action,
    // so Return never confirms a destructive action.
    property AlertAction returnAction: null

    property var visibleActions: [defaultAction]
    readonly property real minimumActionWidth: {
        let width = 0
        for (let i = 0; i < visibleActions.length; ++i) {
            width = Math.max(width, Math.ceil(actionFontMetrics.advanceWidth(visibleActions[i].text)) + 40)
        }
        return width
    }
    readonly property real horizontalActionsWidth: minimumActionWidth * visibleActions.length
        + 10 * Math.max(0, visibleActions.length - 1)

    property FontMetrics actionMetrics: FontMetrics {
        id: actionFontMetrics
        font: Theme.text.buttonStrong.font
    }

    // Escape runs cancel() instead, which answers with the cancel action.
    closePolicy: Popup.NoAutoClose
    closesOnPressOutside: false
    surfaceObjectName: "alertPopupSurface"
    padding: 0
    horizontalPadding: 0
    implicitWidth: Math.max(360, horizontalActionsWidth + 40)
    implicitHeight: columnLayout.implicitHeight

    property Item actionStoreItem: Item {
        id: actionStore
        visible: false
    }
    property AlertAction defaultAction: AlertAction { text: qsTr("OK") }

    function refreshActions() {
        if (actionList && actionList.length > 0) {
            visibleActions = actionList
        } else {
            // Children that aren't actions (such as Connections) also land here.
            const declared = []
            for (let i = 0; i < actionStore.data.length; ++i) {
                if (actionStore.data[i].closesPopup !== undefined) declared.push(actionStore.data[i])
            }
            visibleActions = declared.length > 0 ? declared : [defaultAction]
        }
    }

    function trigger(action) {
        if (!action.enabled) return
        if (action.closesPopup) close()
        action.triggered()
    }

    function cancelAction() {
        if (escapeAction) return escapeAction
        for (let i = 0; i < visibleActions.length; ++i) {
            const action = visibleActions[i]
            if (action.role === AlertAction.Cancel || action.role === AlertAction.Neutral) {
                return action
            }
        }
        return null
    }

    function acceptAction() {
        if (returnAction) return returnAction
        let found = null
        for (let i = 0; i < visibleActions.length; ++i) {
            if (visibleActions[i].role !== AlertAction.Normal) continue
            if (found) return null
            found = visibleActions[i]
        }
        return found
    }

    function accept() {
        if (busy || !opened) return
        const window = contentItem ? contentItem.Window.window : null
        const focused = window ? window.activeFocusItem : null
        // Check boxes and switches ignore Return; it shouldn't submit past them.
        if (focused && focused.autoRepeat !== undefined && focused.checkable !== undefined) return
        const action = acceptAction()
        if (action) trigger(action)
    }

    function cancel() {
        if (busy) return
        const action = cancelAction()
        if (action) {
            trigger(action)
        } else if (dismissible) {
            close()
        }
    }

    Component.onCompleted: refreshActions()
    onAboutToShow: refreshActions()
    onActionListChanged: refreshActions()

    contentItem: ColumnLayout {
        id: columnLayout
        focus: true
        spacing: 0

        Keys.priority: Keys.BeforeItem
        Keys.onEscapePressed: function(event) {
            root.cancel()
            event.accepted = true
        }
        Keys.onReturnPressed: function(event) {
            if (!event.isAutoRepeat) root.accept()
            event.accepted = true
        }
        Keys.onEnterPressed: function(event) {
            if (!event.isAutoRepeat) root.accept()
            event.accepted = true
        }

        Rectangle {
            objectName: "alertIconBadge"
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Theme.space.xl
            Layout.preferredWidth: 56
            Layout.preferredHeight: 56
            visible: root.iconSource.toString().length > 0
            radius: width / 2
            color: root.tone === AlertPopup.Success ? root.toneColor : "transparent"

            Icon {
                objectName: "alertIcon"
                anchors.centerIn: parent
                source: root.iconSource
                color: root.tone === AlertPopup.Success ? Theme.color.accentForeground : root.toneColor
                size: root.tone === AlertPopup.Success ? 28 : 32
            }
        }

        CoreText {
            objectName: root.titleObjectName
            readonly property bool belowIcon: root.iconSource.toString().length > 0
            Layout.fillWidth: true
            Layout.topMargin: belowIcon ? Theme.space.ml : Theme.space.xl
            Layout.leftMargin: Theme.space.xl
            Layout.rightMargin: Theme.space.xl
            text: root.title
            textStyle: Theme.text.subtitle
            color: Theme.color.neutral9
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.WordWrap
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: Theme.space.ml
            Layout.leftMargin: Theme.space.xl
            Layout.rightMargin: Theme.space.xl
            Layout.bottomMargin: Theme.space.xl
            spacing: Theme.space.ml

            // Long messages scroll; the title and actions stay in view.
            Flickable {
                id: messageArea
                objectName: "alertMessageArea"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredHeight: messageText.implicitHeight
                visible: root.message.length > 0
                contentWidth: width
                contentHeight: messageText.implicitHeight
                clip: true
                interactive: contentHeight > height
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {
                    policy: messageArea.interactive ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                }

                CoreText {
                    id: messageText
                    objectName: root.messageObjectName
                    width: messageArea.width
                    text: root.message
                    textFormat: root.messageTextFormat
                    color: Theme.color.neutral8
                    textStyle: Theme.text.description
                    wrapMode: root.messageWrapMode
                    horizontalAlignment: root.messageAlignment
                }
            }

            ColumnLayout {
                id: contentColumn
                Layout.fillWidth: true
                visible: children.length > 0
                spacing: Theme.space.ml
            }

            CoreText {
                objectName: root.errorTextObjectName
                Layout.fillWidth: true
                visible: text.length > 0
                text: root.errorText
                color: Theme.color.danger
                textStyle: Theme.text.description
                wrapMode: Text.WordWrap
                horizontalAlignment: root.messageAlignment
            }
        }

        Separator {
            Layout.fillWidth: true
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
            Layout.topMargin: Theme.space.xl
            Layout.leftMargin: Theme.space.xl
            Layout.rightMargin: Theme.space.xl
            Layout.bottomMargin: Theme.space.xl
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
                    height: Theme.size.controlLarge

                    function triggerAction() {
                        root.trigger(alertAction)
                    }

                    NeutralButton {
                        anchors.fill: parent
                        visible: actionDelegate.neutralAction
                        objectName: visible ? actionDelegate.alertAction.buttonObjectName : ""
                        text: actionDelegate.alertAction.text
                        enabled: actionDelegate.alertAction.enabled
                        buttonSize: NeutralButton.Large
                        onClicked: actionDelegate.triggerAction()
                    }

                    PrimaryButton {
                        anchors.fill: parent
                        horizontalPadding: Theme.space.xl
                        visible: !actionDelegate.neutralAction
                        objectName: visible ? actionDelegate.alertAction.buttonObjectName : ""
                        text: actionDelegate.alertAction.text
                        enabled: actionDelegate.alertAction.enabled
                        destructive: actionDelegate.alertAction.role === AlertAction.Destructive
                        onClicked: actionDelegate.triggerAction()
                    }
                }
            }
        }
    }
}
