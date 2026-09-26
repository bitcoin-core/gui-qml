// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import "../controls"

Popup {
    id: root
    property Component initialPage: null
    property string closeButtonObjectName: "onboardingSettingsCloseButton"
    property real verticalOffset: 0
    readonly property alias pageStack: pages

    parent: Overlay.overlay
    width: Math.min(532, parent ? parent.width - 32 : 532)
    height: Math.min(504, parent ? parent.height - 40 : 504)
    x: parent ? (parent.width - width) / 2 : 0
    y: (parent ? (parent.height - height) / 2 : 0) + verticalOffset
    padding: 0
    transformOrigin: Popup.Center
    modal: true
    dim: true
    focus: true
    closePolicy: Popup.CloseOnEscape

    background: Rectangle {
        radius: 16
        color: Theme.color.neutral1
        border.width: 1
        border.color: Theme.color.neutral3
        SurfaceGradientBorder {
            anchors.fill: parent
            surfaceColor: parent.color
            cornerRadius: parent.radius
        }
    }

    Overlay.modal: Rectangle {
        color: Qt.rgba(0, 0, 0, 0.6)
        opacity: root.opacity
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
        NumberAnimation {
            property: "scale"
            from: 0.96
            to: 1
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
        NumberAnimation {
            property: "scale"
            from: 1
            to: 0.96
            duration: 250
            easing.type: Easing.InCubic
        }
    }

    contentItem: Item {
        clip: true

        PageStack {
            id: pages
            anchors.fill: parent
            initialItem: root.initialPage
        }

        CloseButton {
            objectName: root.closeButtonObjectName
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 16
            z: 2
            buttonSize: CloseButton.Medium
            backgroundColor: Theme.color.neutral2
            onClicked: root.close()
        }
    }

    onClosed: if (pages.depth > 1) pages.pop(null, StackView.Immediate)
}
