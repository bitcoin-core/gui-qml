// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../controls"

Rectangle {
    id: root

    property url iconSource: ""
    property color tintColor: Theme.color.info
    property color iconColor: tintColor
    property string text: ""
    property string textObjectName: ""
    property color textColor: Theme.color.textFor(tintColor)
    property var messageTextStyle: Theme.text.description
    property color backgroundColor: Theme.color.tint(tintColor)
    property bool showsCloseButton: false
    property string actionText: ""
    // When > 0, auto-emits dismissed() after this many seconds while visible.
    property int dismissAfter: 0
    // Bind shown rather than visible, so the banner fades out instead of snapping.
    property bool shown: true
    // The last message, shown while the banner fades out after its text is cleared.
    property string _displayedText: text

    signal actionTriggered()
    signal dismissed()

    color: backgroundColor
    radius: Theme.radius.card
    implicitHeight: contentRow.height + 16
    opacity: 0
    visible: shown || hideAnim.running

    onTextChanged: if (text.length > 0) _displayedText = text
    onShownChanged: {
        if (shown) {
            // Shown again mid fade-out: visible never went false, so fade back in here.
            if (hideAnim.running) {
                hideAnim.stop()
                fadeInAnim.restart()
            }
            return
        }
        fadeInAnim.stop()
        fadeOutAnim.stop()
        dismissTimer.stop()
        hideAnim.restart()
    }

    onVisibleChanged: {
        if (visible) {
            fadeOutAnim.stop()
            hideAnim.stop()
            dismissTimer.stop()
            fadeInAnim.restart()
        } else {
            fadeInAnim.stop()
            fadeOutAnim.stop()
            dismissTimer.stop()
            opacity = 0
        }
    }

    NumberAnimation {
        id: fadeInAnim
        target: root
        property: "opacity"
        to: 1
        duration: Theme.motion.normal
        easing.type: Theme.motion.easing.enter
        onStopped: {
            if (root.dismissAfter > 0) {
                dismissTimer.start()
            }
        }
    }

    NumberAnimation {
        id: fadeOutAnim
        target: root
        property: "opacity"
        to: 0
        duration: Theme.motion.fast
        easing.type: Theme.motion.easing.exit
        onStopped: root.dismissed()
    }

    NumberAnimation {
        id: hideAnim
        target: root
        property: "opacity"
        to: 0
        duration: Theme.motion.fast
        easing.type: Theme.motion.easing.exit
    }

    Timer {
        id: dismissTimer
        interval: root.dismissAfter * 1000
        onTriggered: fadeOutAnim.start()
    }

    Item {
        id: contentFrame
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: Theme.space.l
        anchors.rightMargin: Theme.space.l
        height: contentRow.height
    }

    RowLayout {
        id: contentRow
        parent: contentFrame
        width: parent.width
        spacing: Theme.space.m

        Icon {
            objectName: root.objectName !== "" ? root.objectName + "Icon" : ""
            visible: root.iconSource != ""
            source: root.iconSource
            color: root.iconColor
            size: Theme.size.icon.xl
            Layout.preferredWidth: 24
            Layout.preferredHeight: 24
            Layout.alignment: Qt.AlignVCenter
        }

        CoreText {
            objectName: root.textObjectName
            Layout.fillWidth: true
            Layout.rightMargin: root.iconSource != ""
                && root.actionText === ""
                && !root.showsCloseButton
                ? 24
                : 0
            text: hideAnim.running ? root._displayedText : root.text
            color: root.textColor
            textStyle: root.messageTextStyle
            horizontalAlignment: root.iconSource != "" ? Text.AlignLeft : Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.WordWrap
        }

        Button {
            id: actionButton
            visible: root.actionText !== ""
            text: root.actionText
            padding: Theme.space.s
            focusPolicy: Qt.StrongFocus
            background: Item {
                FocusBorder {
                    visible: actionButton.visualFocus
                    topMargin: 0
                    bottomMargin: 0
                    leftMargin: 0
                    rightMargin: 0
                    borderRadius: 6
                }
            }
            contentItem: CoreText {
                text: actionButton.text
                color: root.textColor
                textStyle: Theme.text.description
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                opacity: actionButton.hovered ? 0.75 : 1.0
            }
            onClicked: root.actionTriggered()
            HoverHandler {
                cursorShape: Qt.PointingHandCursor
            }
        }

        Rectangle {
            visible: root.actionText !== "" && root.showsCloseButton
            Layout.preferredWidth: 1
            Layout.preferredHeight: 20
            color: Qt.rgba(root.textColor.r, root.textColor.g, root.textColor.b, 0.4)
        }

        IconButton {
            id: closeButton
            objectName: root.objectName !== "" ? root.objectName + "CloseButton" : ""
            visible: root.showsCloseButton
            size: 24
            iconSize: Theme.size.icon.s
            iconSource: "image://images/cross"
            iconColor: root.textColor
            hoverColor: root.textColor
            activeColor: root.textColor
            accessibleName: qsTr("Dismiss")
            background: Rectangle {
                radius: closeButton.backgroundRadius
                color: Qt.rgba(root.textColor.r, root.textColor.g, root.textColor.b,
                    closeButton.down ? 0.2 : closeButton.hovered ? 0.12 : 0)
            }
            onClicked: root.dismissed()
        }
    }
}
