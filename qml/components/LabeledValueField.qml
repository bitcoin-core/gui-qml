// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15

import "../controls"

Item {
    id: root

    property string labelText: ""
    property alias text: valueText.text
    property bool wrap: true
    property color valueColor: Theme.color.neutral9
    property int labelPixelSize: 18
    property color labelColor: Theme.color.neutral9
    property int valueHorizontalAlignment: Text.AlignLeft

    Layout.fillWidth: true
    implicitHeight: Math.max(label.implicitHeight, valueText.implicitHeight)

    CoreText {
        id: label
        width: 110
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        horizontalAlignment: Text.AlignLeft
        text: root.labelText
        font.pixelSize: root.labelPixelSize
        color: root.labelColor
    }

    CoreText {
        id: valueText
        anchors.left: label.right
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        horizontalAlignment: root.valueHorizontalAlignment
        wrap: root.wrap
        font.pixelSize: 18
        color: root.valueColor
    }
}
