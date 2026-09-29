// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import "../../controls"

// Titles retain their base Theme size independently of the widget's fontScale.
CoreText {
    property var textStyle: Theme.text.widgetTitle
    font: textStyle.font
    color: Theme.color.neutral7
    horizontalAlignment: Text.AlignLeft
    wrap: false
    elide: Text.ElideRight
}
