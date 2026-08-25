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
    property color iconColor: Theme.color.white
    property string text: ""
    property string textObjectName: ""
    property color textColor: Theme.color.white
    property color backgroundColor: Theme.color.neutral2
    property bool showsCloseButton: false
    property string actionText: ""
    // When > 0, auto-emits dismissed() after this many seconds while visible.
    property int dismissAfter: 0

    signal actionTriggered()
    signal dismissed()

    color: backgroundColor
    radius: 5
    implicitHeight: Math.max(50, contentRow.implicitHeight + 20)
    // Follows the initial visibility instead of always starting transparent.
    // A banner whose condition already holds when it is built inside an
    // already visible page never sees visible change, so the fade-in below
    // would not run and the banner would keep its space in the layout while
    // drawing nothing. Whether that happens depends on creation order (a page
    // whose tree becomes visible only after construction still gets the
    // change), so both orders must draw. The animations assign opacity
    // directly, which drops this binding once one of them runs.
    opacity: visible ? 1 : 0

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

    // Shown outright rather than faded in, so the auto-dismiss countdown that
    // normally starts when the fade completes has to be started here instead.
    Component.onCompleted: {
        if (root.opacity === 1 && root.dismissAfter > 0) dismissTimer.start()
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
        anchors.leftMargin: 20
        anchors.rightMargin: 12
        spacing: 12

        Icon {
            visible: root.iconSource != ""
            source: root.iconSource
            color: root.iconColor
            size: 18
            Layout.alignment: Qt.AlignVCenter
        }

        CoreText {
            objectName: root.textObjectName
            Layout.fillWidth: true
            text: root.text
            // Banner text is a message, never markup. Without this the Text
            // default of AutoText would silently upgrade markup-looking
            // strings (an error can interpolate a filesystem path) to rich
            // text.
            textFormat: Text.PlainText
            color: root.textColor
            font: Theme.text.description.font
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
