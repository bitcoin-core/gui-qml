// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: root

    property alias text: tooltipText.text
    property string textObjectName: ""
    property url iconSource: ""
    property color iconColor: Theme.color.neutral9
    property int iconSize: 14
    property color textColor: Theme.color.neutral9
    property color backgroundColor: Theme.color.neutral0
    property color borderColor: Theme.color.neutral4
    property bool arrowAtBottom: false
    property bool centerBubbleOnArrow: false
    property int horizontalPadding: 15
    property int verticalPadding: 10
    property int contentSpacing: 6
    property int arrowHorizontalInset: 10
    property int arrowWidth: 22
    property int arrowHeight: 10
    property var textStyle: Theme.text.description

    implicitWidth: tooltipBg.width
    implicitHeight: tooltipBg.height + arrow.height - 1

    Rectangle {
        id: tooltipBg
        color: root.backgroundColor
        border.color: root.borderColor
        radius: 5
        border.width: 1
        width: contentRow.implicitWidth + 2 * root.horizontalPadding
        height: contentRow.implicitHeight + 2 * root.verticalPadding
        x: root.centerBubbleOnArrow
            ? (root.width - width) / 2
            : arrow.x + arrow.width + root.arrowHorizontalInset - width
        y: root.arrowAtBottom ? 0 : arrow.height - 1
    }

    Canvas {
        id: arrow
        width: root.arrowWidth
        height: root.arrowHeight
        rotation: root.arrowAtBottom ? 180 : 0
        anchors.horizontalCenter: root.horizontalCenter
        y: root.arrowAtBottom ? tooltipBg.y + tooltipBg.height - 1 : 0

        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            ctx.beginPath()
            ctx.moveTo(width / 2, 1)
            ctx.lineTo(width - 1, height)
            ctx.lineTo(1, height)
            ctx.closePath()
            ctx.fillStyle = root.backgroundColor
            ctx.fill()
            ctx.strokeStyle = root.borderColor
            ctx.lineWidth = 1
            ctx.stroke()
        }
    }

    Connections {
        target: root
        function onBackgroundColorChanged() { arrow.requestPaint() }
        function onBorderColorChanged() { arrow.requestPaint() }
        function onArrowAtBottomChanged() { arrow.requestPaint() }
    }

    RowLayout {
        id: contentRow
        anchors.centerIn: tooltipBg
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
            font: root.textStyle.font
            wrapMode: Text.NoWrap
            Layout.alignment: Qt.AlignVCenter
        }
    }
}
