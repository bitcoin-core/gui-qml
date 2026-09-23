// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15

import "../controls"

Item {
    id: root

    property Item popupAnchor: null
    property int popupAnchorHorizontalOffset: 0
    property int popupOffset: 8
    property int popupMargin: 8
    property int visibleDurationMs: 1500
    property string text: ""
    property string textObjectName: ""
    property url iconSource: ""
    property color iconColor: Theme.color.neutral9
    property int iconSize: 14
    property color textColor: Theme.color.neutral9
    property color backgroundColor: Theme.color.neutral1
    property color borderColor: Theme.color.neutral2
    property int horizontalPadding: 10
    property int verticalPadding: 4
    property int spacing: 4
    property var textStyle: Theme.text.caption

    visible: popupToast.opened
    implicitWidth: 0
    implicitHeight: 0

    function show(anchor, horizontalOffset) {
        if (anchor !== undefined && anchor !== null) {
            root.popupAnchor = anchor
        }
        root.popupAnchorHorizontalOffset = horizontalOffset !== undefined ? horizontalOffset : 0

        popupToast.open()
        Qt.callLater(function() {
            root.updatePopupPosition()
        })
        hideTimer.restart()
    }

    function hide() {
        hideTimer.stop()
        popupToast.close()
    }

    function updatePopupPosition() {
        if (!root.popupAnchor || !Overlay.overlay) {
            return
        }
        var anchorPos = root.popupAnchor.mapToItem(Overlay.overlay, 0, 0)
        var preferredX = anchorPos.x + (root.popupAnchor.width - popupToast.width) / 2 + root.popupAnchorHorizontalOffset
        var maxX = Math.max(root.popupMargin, Overlay.overlay.width - popupToast.width - root.popupMargin)
        popupToast.x = Math.min(Math.max(root.popupMargin, preferredX), maxX)
        popupToast.y = Math.max(root.popupMargin, anchorPos.y - popupToast.height - root.popupOffset)
    }

    Timer {
        id: hideTimer
        interval: root.visibleDurationMs
        onTriggered: root.hide()
    }

    Popup {
        id: popupToast

        parent: Overlay.overlay
        padding: 0
        modal: false
        dim: false
        closePolicy: Popup.NoAutoClose
        width: popupContent.implicitWidth
        height: popupContent.implicitHeight
        background: null

        onOpened: Qt.callLater(function() {
            root.updatePopupPosition()
        })
        onWidthChanged: root.updatePopupPosition()
        onHeightChanged: root.updatePopupPosition()

        enter: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 150 }
        }

        exit: Transition {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 150 }
        }

        contentItem: Tooltip {
            id: popupContent
            width: implicitWidth
            height: implicitHeight
            text: root.text
            textObjectName: root.textObjectName
            iconSource: root.iconSource
            iconColor: root.iconColor
            iconSize: root.iconSize
            textColor: root.textColor
            backgroundColor: root.backgroundColor
            borderColor: root.borderColor
            horizontalPadding: root.horizontalPadding
            verticalPadding: root.verticalPadding
            contentSpacing: root.spacing
            textStyle: root.textStyle
        }
    }
}
