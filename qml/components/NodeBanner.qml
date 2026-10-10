// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../controls"

// Closed warnings stay dismissed; other notices return when they change.
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
    implicitHeight: shown ? content.implicitHeight + 2 * Theme.space.ml : 0
    color: Theme.color.tint(status.bannerColor)

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
        anchors.leftMargin: Theme.space.xl
        anchors.rightMargin: Theme.space.ml
        spacing: Theme.space.ml

        Rectangle {
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: Theme.space.s
            implicitWidth: Theme.space.m
            implicitHeight: Theme.space.m
            radius: height / 2
            color: root.status.bannerColor
        }
        CoreText {
            objectName: "nodeBannerText"
            Layout.fillWidth: true
            textFormat: Text.StyledText
            text: "<b>" + root.escaped(root.status.bannerTitle) + "</b> " + root.escaped(root.status.bannerText)
            textStyle: Theme.text.description
            horizontalAlignment: Text.AlignLeft
            verticalAlignment: Text.AlignTop
        }
        CloseButton {
            objectName: "nodeBannerCloseButton"
            Layout.alignment: Qt.AlignTop
            backgroundColor: "transparent"
            backgroundHoverColor: Theme.color.surfaceHover
            backgroundPressedColor: Theme.color.surfacePressed
            Accessible.name: qsTr("Close notice")
            onClicked: root.dismiss()
        }
    }
}
