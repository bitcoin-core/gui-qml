// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15

CoreText {
    id: unitGlyph
    required property color viewColor
    required property font viewFont
    required property real viewShiftProgress
    required property real viewUnitProgress
    required property var modelData
    objectName: "amountUnit_" + modelData.glyph
    text: modelData.glyph
    font: unitGlyph.viewFont
    color: unitGlyph.viewColor
    wrap: false
    horizontalAlignment: Text.AlignLeft
    anchors.verticalCenter: parent.verticalCenter
    width: modelData.width
    x: modelData.fromX + (modelData.toX - modelData.fromX) * unitGlyph.viewShiftProgress
    opacity: modelData.fromOpacity + (modelData.toOpacity - modelData.fromOpacity) * unitGlyph.viewUnitProgress
    Accessible.ignored: true
}
