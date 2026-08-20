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
    property color tintColor: Theme.color.blue
    property real backgroundOpacity: 0.25
    property color iconColor: tintColor
    property string text: ""
    property string textObjectName: ""
    property color textColor: tintColor
    property color backgroundColor: Qt.rgba(
        tintColor.r,
        tintColor.g,
        tintColor.b,
        backgroundOpacity)
    property bool showsCloseButton: false
    property string actionText: ""
    // When > 0, auto-emits dismissed() after this many seconds while visible.
    property int dismissAfter: 0

    signal actionTriggered()
    signal dismissed()

    color: backgroundColor
    radius: 15
    implicitHeight: contentRow.implicitHeight + 16
    opacity: 0

    onVisibleChanged: {
        if (visible) {
            fadeOutAnim.stop()
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
        from: 0
        to: 1
        duration: 150
        easing.type: Easing.OutCubic
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
        from: 1
        to: 0
        duration: 150
        easing.type: Easing.InCubic
        onStopped: root.dismissed()
    }

    Timer {
        id: dismissTimer
        interval: root.dismissAfter * 1000
        onTriggered: fadeOutAnim.start()
    }

    RowLayout {
        id: contentRow
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 15
        anchors.rightMargin: 15
        spacing: 8

        Icon {
            objectName: root.objectName !== "" ? root.objectName + "Icon" : ""
            visible: root.iconSource != ""
            source: root.iconSource
            color: root.iconColor
            size: 24
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
            text: root.text
            color: root.textColor
            font.pixelSize: 15
            lineHeightMode: Text.FixedHeight
            lineHeight: 21
            horizontalAlignment: root.iconSource != "" ? Text.AlignLeft : Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.WordWrap
        }

        Button {
            id: actionButton
            visible: root.actionText !== ""
            text: root.actionText
            padding: 6
            background: null
            contentItem: CoreText {
                text: actionButton.text
                color: root.textColor
                font: Theme.text.description.font
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

        Icon {
            visible: root.showsCloseButton
            source: "image://images/cross"
            color: root.textColor
            size: 14
            enabled: true
            padding: 6
            onClicked: root.dismissed()
            HoverHandler {
                cursorShape: Qt.PointingHandCursor
            }
        }
    }
}
