// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15

import "../controls"

// Node action from the block clock design: orange when it is the way out of
// the current state, outlined otherwise.
ContinueButton {
    id: root
    property bool accent: false

    implicitHeight: 36
    leftPadding: 14
    rightPadding: 14
    backgroundRadius: 6
    textFontPixelSize: 14
    textColor: accent ? Theme.color.white : Theme.color.neutral9
    backgroundColor: accent ? Theme.color.orange : "transparent"
    backgroundHoverColor: accent ? Theme.color.orangeLight1 : Theme.color.neutral2
    backgroundPressedColor: accent ? Theme.color.orangeLight2 : Theme.color.neutral3
    // Accent buttons keep the app's primary gradient border.
    borderColor: accent ? "transparent" : Theme.color.neutral4
    borderHoverColor: borderColor
    borderPressedColor: borderColor
}
