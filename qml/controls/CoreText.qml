// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15

Text {
    // A Theme.text role: sets the font, its line height and Text.FixedHeight together.
    // Font properties are bound one by one so callers can still override, say, pixelSize.
    property var textStyle: null
    property bool bold: false
    property bool wrap: true
    color: enabled ? Theme.color.neutral9 : Theme.color.disabledContent
    linkColor: Theme.color.accentText
    font.family: textStyle ? textStyle.family : Theme.text.family
    font.weight: textStyle ? textStyle.weight : bold ? Theme.text.strongWeight : Theme.text.regularWeight
    font.pixelSize: textStyle ? textStyle.pixelSize : 13
    lineHeight: textStyle ? textStyle.lineHeight : 1
    lineHeightMode: textStyle ? Text.FixedHeight : Text.ProportionalHeight
    horizontalAlignment: Text.AlignHCenter
    verticalAlignment: Text.AlignVCenter
    wrapMode: wrap ? Text.WordWrap : Text.NoWrap

    Behavior on color {
        enabled: Theme.motion.animated
        ColorAnimation { duration: Theme.motion.normal }
    }
}
