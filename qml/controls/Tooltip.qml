// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15

Item {
    id: root

    property alias text: tooltipText.text
    property string textObjectName: ""
    property url iconSource: ""
    property color iconColor: Theme.color.neutral9
    property int iconSize: Theme.size.icon.s
    property color textColor: Theme.color.neutral9
    property color backgroundColor: Theme.color.surface1
    property color borderColor: Theme.color.menuBorder
    property bool shown: true
    property bool motionEnabled: Theme.motion.animated
    property int horizontalPadding: Theme.space.ml
    property int verticalPadding: Theme.space.m
    property int windowMargin: Theme.space.m
    property int contentSpacing: Theme.space.s
    property var textStyle: Theme.text.description
    property bool _revealed: false
    property bool _ready: false
    readonly property real _maximumWidth: Window.window
        ? Math.max(1, Window.window.width - 2 * windowMargin) : Number.MAX_VALUE
    readonly property real _horizontalOffset: {
        if (!Window.window || !parent) return 0
        let left = x
        for (let item = parent; item; item = item.parent) left += item.x
        const boundedLeft = Math.max(windowMargin,
            Math.min(left, Window.window.width - width - windowMargin))
        return boundedLeft - left
    }

    implicitWidth: tooltipBg.width
    implicitHeight: tooltipBg.height
    visible: _revealed || opacity > 0
    opacity: _revealed ? 1 : 0
    scale: _revealed || Theme.motion.reduced ? 1 : 0.98
    transformOrigin: Item.Top
    transform: Translate { x: root._horizontalOffset }

    onShownChanged: {
        if (!_ready) return
        showTimer.stop()
        if (!shown || !motionEnabled || opacity > 0) {
            _revealed = shown
        } else {
            showTimer.start()
        }
    }

    onMotionEnabledChanged: {
        if (!motionEnabled) {
            showTimer.stop()
            _revealed = shown
        }
    }

    Component.onCompleted: {
        _revealed = shown
        _ready = true
    }

    Timer {
        id: showTimer
        interval: Theme.motion.tooltipDelay
        onTriggered: root._revealed = root.shown
    }

    Behavior on opacity {
        enabled: root.motionEnabled && root._ready
        NumberAnimation {
            duration: root._revealed ? Theme.motion.normal : Theme.motion.fast
            easing.type: root._revealed ? Theme.motion.easing.enter : Theme.motion.easing.exit
        }
    }

    Behavior on scale {
        enabled: root.motionEnabled && root._ready
        NumberAnimation {
            duration: root._revealed ? Theme.motion.normal : Theme.motion.fast
            easing.type: root._revealed ? Theme.motion.easing.enter : Theme.motion.easing.exit
        }
    }

    Rectangle {
        id: tooltipBg
        color: root.backgroundColor
        border.color: root.borderColor
        radius: Theme.radius.m
        border.width: 1
        width: Math.min(contentRow.implicitWidth + 2 * root.horizontalPadding, root._maximumWidth)
        height: contentRow.implicitHeight + 2 * root.verticalPadding
    }

    RowLayout {
        id: contentRow
        anchors.centerIn: tooltipBg
        width: Math.max(0, tooltipBg.width - 2 * root.horizontalPadding)
        spacing: root.contentSpacing

        Icon {
            visible: root.iconSource != ""
            source: root.iconSource
            color: root.iconColor
            size: root.iconSize
            Layout.alignment: Qt.AlignVCenter
        }

        CoreText {
            id: tooltipText
            objectName: root.textObjectName
            text: ""
            color: root.textColor
            textStyle: root.textStyle
            wrapMode: Text.NoWrap
            elide: Text.ElideRight
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
        }
    }
}
