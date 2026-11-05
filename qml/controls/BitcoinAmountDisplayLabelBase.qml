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
    // Shows a fixed mask instead while the Hide amounts setting is on. Off by
    // default: amounts people must check, as on Send, stay visible.
    property bool hidesAmount: false
    readonly property bool amountHidden: hidesAmount && typeof optionsModel !== "undefined" && optionsModel
        && optionsModel.hideAmounts === true
    readonly property string maskText: "∗∗∗∗∗∗∗∗∗∗"
    readonly property string shownText: amountHidden ? maskText : text
    property string displayText: shownText
    property alias textStyle: label.textStyle
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
    // The digit roll moves text, so reduced motion shows the new value at once.
    readonly property bool rollsUnitChanges: animateUnitChanges && !Theme.motion.reduced
    property bool animating: false
    property bool supportsAnimation: false

    readonly property real naturalWidth: label.implicitWidth

    textStyle: Theme.text.amountDescription

    implicitWidth: naturalWidth
    implicitHeight: label.implicitHeight
    baselineOffset: label.baselineOffset
    Accessible.role: Accessible.StaticText
    Accessible.name: amountHidden ? qsTr("Hidden amount") : text

    CoreText {
        id: label
        objectName: "amountStaticText"
        text: root.displayText
        anchors.fill: parent
        visible: !root.animating
        wrap: false
        Accessible.ignored: true
    }
}
