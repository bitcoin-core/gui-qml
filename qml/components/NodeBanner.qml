// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../controls"

// Full-width notice for node errors, connection loss and Core warnings.
// Closing a warning dismisses it until its text changes, across restarts.
// Errors and connection loss describe the current state, so closing them
// only hides that message until it changes.
Rectangle {
    id: root
    objectName: "nodeBanner"

    property NodeStatus status: NodeStatus {}
    readonly property string message: status.bannerKind.length > 0
        ? status.bannerKind + "\n" + status.bannerText : ""
    property string dismissedMessage: ""
    readonly property bool shown: message.length > 0 && message !== dismissedMessage

    function dismiss() {
        if (root.status.bannerKind === "warning") {
            root.status.dismissWarnings(root.status.activeWarnings)
        } else {
            root.dismissedMessage = root.message
        }
    }

    function escaped(text) {
        return text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/\n/g, "<br>")
    }

    visible: shown
    implicitHeight: shown ? content.implicitHeight + 24 : 0
    color: Qt.rgba(status.bannerColor.r, status.bannerColor.g, status.bannerColor.b, status.bannerKind === "error" ? 0.16 : 0.14)

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: root.status.bannerColor
    }

    RowLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 20
        anchors.rightMargin: 12
        spacing: 12

        Rectangle {
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 6
            implicitWidth: 8
            implicitHeight: 8
            radius: 4
            color: root.status.bannerColor
        }
        CoreText {
            objectName: "nodeBannerText"
            Layout.fillWidth: true
            textFormat: Text.StyledText
            text: "<b>" + root.escaped(root.status.bannerTitle) + "</b> " + root.escaped(root.status.bannerText)
            font.pixelSize: 14
            lineHeight: 1.45
            horizontalAlignment: Text.AlignLeft
            verticalAlignment: Text.AlignTop
        }
        CloseButton {
            objectName: "nodeBannerCloseButton"
            Layout.alignment: Qt.AlignTop
            size: 24
            iconSize: 8
            backgroundColor: "transparent"
            Accessible.name: qsTr("Close notice")
            onClicked: root.dismiss()
        }
    }
}
