// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15

QtObject {
    required property string typeSetId
    required property string family
    required property string monoFamily

    // Faces are picked by weight, not style name: style names differ between
    // families, and Qt.font() ignores styleName on Qt 6.2 and 6.4.
    property int regularWeight: Font.Normal
    property int strongWeight: Font.DemiBold
    property int monoWeight: Font.Normal
}
