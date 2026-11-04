// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15

import "../controls"

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
    borderColor: accent ? "transparent" : Theme.color.neutral4
    borderHoverColor: borderColor
    borderPressedColor: borderColor
}
