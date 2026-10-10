// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15

import "../controls"

PrimaryButton {
    id: root
    property bool accent: false

    implicitHeight: Math.max(Theme.size.control, implicitContentHeight + topPadding + bottomPadding)
    topPadding: Theme.space.xs
    bottomPadding: Theme.space.xs
    leftPadding: Theme.space.l
    rightPadding: Theme.space.l
    textStyle: Theme.text.captionStrong
    textColor: accent ? Theme.color.accentForeground : Theme.color.neutral9
    backgroundColor: accent ? Theme.color.accent : "transparent"
    backgroundHoverColor: accent ? Theme.color.accentHover : Theme.color.surfaceHover
    backgroundPressedColor: accent ? Theme.color.accentPressed : Theme.color.surfacePressed
    borderColor: accent ? "transparent" : Theme.color.controlBorder
    borderHoverColor: borderColor
    borderPressedColor: borderColor
}
