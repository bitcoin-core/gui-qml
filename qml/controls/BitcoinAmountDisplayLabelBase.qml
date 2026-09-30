// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15

Item {
    id: root

    property string amount: ""
    property string unit: ""
    property bool unitPrefix: false
    // text remains available for models that already expose a formatted amount.
    property string text: !amount.length || !unit.length ? amount
        : unitPrefix ? unit + " " + amount : amount + " " + unit
    property string displayText: text
    property alias font: label.font
    property alias color: label.color
    property alias horizontalAlignment: label.horizontalAlignment
    property alias elide: label.elide
    property alias wrap: label.wrap
    property alias fontSizeMode: label.fontSizeMode
    property alias minimumPixelSize: label.minimumPixelSize
    property alias lineHeight: label.lineHeight
    property alias lineHeightMode: label.lineHeightMode
    property bool animateUnitChanges: true
    property bool animating: false
    property bool supportsAnimation: false

    readonly property real naturalWidth: label.implicitWidth

    implicitWidth: naturalWidth
    implicitHeight: label.implicitHeight
    baselineOffset: label.baselineOffset
    Accessible.role: Accessible.StaticText
    Accessible.name: text

    CoreText {
        id: label
        objectName: "amountStaticText"
        text: root.displayText
        anchors.fill: parent
        visible: !root.animating
        wrap: false
    }
}
