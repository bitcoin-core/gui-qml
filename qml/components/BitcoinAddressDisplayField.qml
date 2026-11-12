// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15

import "../controls"

Item {
    id: root

    property string labelText: qsTr("Send to")
    property string text: ""
    property string fullText: ""
    property bool expanded: false
    property int labelPixelSize: 18
    property color labelColor: Theme.color.neutral9
    readonly property bool showLabel: labelText.length > 0
    readonly property bool expandable: fullText.length > 0
    readonly property string displayText: expanded ? fullText : text

    Layout.fillWidth: true
    implicitHeight: Math.max(showLabel ? label.implicitHeight + 6 : 0, addressContainer.implicitHeight)

    onExpandableChanged: {
        if (!expandable) {
            expanded = false
        }
    }

    function click() {
        if (expandable) {
            expanded = !expanded
        }
    }

    CoreText {
        id: label
        visible: root.showLabel
        width: 110
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.topMargin: 6
        horizontalAlignment: Text.AlignLeft
        text: root.labelText
        font.pixelSize: root.labelPixelSize
        color: root.labelColor
    }

    Item {
        id: addressContainer
        anchors.left: root.showLabel ? label.right : parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        implicitHeight: Math.max(addressText.implicitHeight, 32)
        height: implicitHeight

        CoreText {
            id: addressText
            objectName: root.objectName + "Text"
            width: parent.width
            text: root.displayText
            wrap: root.expanded
            elide: root.expanded ? Text.ElideNone : Text.ElideRight
            horizontalAlignment: root.expandable ? Text.AlignRight : Text.AlignLeft
            verticalAlignment: Text.AlignTop
            font: Theme.text.monoBody.font
            lineHeight: Theme.text.monoBody.lineHeight
            lineHeightMode: Text.FixedHeight
            color: Theme.color.neutral9

            HoverHandler {
                enabled: root.expandable
                cursorShape: Qt.PointingHandCursor
            }

            TapHandler {
                enabled: root.expandable
                onTapped: root.click()
            }
        }
    }
}
