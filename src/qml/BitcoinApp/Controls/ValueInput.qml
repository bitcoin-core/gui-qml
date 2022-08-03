// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls

TextEdit {
    id: root
    property string description: ""
    property int descriptionSize: 18

    font.family: "Inter"
    font.styleName: "Regular"
    font.pixelSize: root.descriptionSize
    color: Theme.color.neutral8
    text: description
    horizontalAlignment: Text.AlignRight
    wrapMode: Text.WordWrap
}
